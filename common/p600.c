////////////////////////////////////////////////////////////////////////////////
// Top level code
////////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "p600.h"

#include "scanner.h"
#include "display.h"
#include "synth.h"
#include "potmux.h"
#include "adsr.h"
#include "lfo.h"
#include "tuner.h"
#include "assigner.h"
#include "storage.h"

#define UNROLL_VOICES // undefine this to save code size, at the expense of speed

#define P600_MONO_ENV 0 // informative constant, don't change it!
#define P600_BENDER_OFFSET -16384

#define ENV_EXPO 1
#define ENV_SLOW 2

const p600Pot_t continuousParameterToPot[cpCount]=
{
	ppFreqA,ppMixer,ppAPW,
	ppFreqB,ppGlide,ppBPW,ppFreqBFine,
	ppCutoff,ppResonance,ppFilEnvAmt,
	ppFilRel,ppFilSus,ppFilDec,ppFilAtt,
	ppAmpRel,ppAmpSus,ppAmpDec,ppAmpAtt,
	ppPModFilEnv,ppPModOscB,
	ppLFOFreq,ppLFOAmt,
	ppSpeed,
};

const p600Button_t bitParameterToButton[11]=
{
	pbASaw,pbATri,pbASqr,pbSync,
	pbBSaw,pbBTri,pbBSqr,
	pbPModFA,pbPModFil,
	pbLFOShape,
	pbUnison,
};

static struct
{
	struct adsr_s filEnvs[P600_VOICE_COUNT];
	struct adsr_s ampEnvs[P600_VOICE_COUNT];

	struct lfo_s lfo;
	
	uint16_t oscANoteCV[P600_VOICE_COUNT];
	uint16_t oscBNoteCV[P600_VOICE_COUNT];
	uint16_t filterNoteCV[P600_VOICE_COUNT]; 
	
	uint16_t oscATargetCV[P600_VOICE_COUNT];
	uint16_t oscBTargetCV[P600_VOICE_COUNT];
	uint16_t filterTargetCV[P600_VOICE_COUNT];


	int16_t benderRawPosition;
	int16_t benderAmount;
	int16_t benderCVs[pcFil6-pcOsc1A+1];
	int16_t benderVolumeCV;

	int8_t gliding;
	
	int8_t presetDigit; // -1: none / 0: load decade digit / 1: store decade digit / 2: load unit digit / 3: store unit digit	
	int8_t presetAwaitingNumber;
} p600;

static const char * modulationName(modulation_t m)
{
	switch(m)
	{
	case modPitch:
		return "Pitch";
	case modFilter:
		return "Filter";
	case modVolume:
		return "Volume";
	case modPW:
		return "PWM";
	case modResonance:
		return "Resonance";
	case modMixer:
		return "Mixer";
	default:
		return "Off";
	}
}

static void computeTunedCVs(void)
{
	int32_t baseCutoff;
	uint16_t cva,cvb,cvf,baseAPitch,baseBPitch;
	int16_t mTune,fineBFreq;
	uint8_t note,baseCutoffNote,baseANote,baseBNote;
	int8_t v;
	
	// filters and oscs
	
	mTune=(potmux_getValue(ppMTune)>>7)+INT8_MIN*2;
	fineBFreq=(currentPreset.continuousParameters[cpFreqBFine]>>7)+INT8_MIN*2;
	
	baseCutoff=((uint32_t)currentPreset.continuousParameters[cpCutoff]*5)>>3; // 62.5% of raw cutoff
	baseAPitch=currentPreset.continuousParameters[cpFreqA]>>2;
	baseBPitch=currentPreset.continuousParameters[cpFreqB]>>2;

	baseCutoffNote=baseCutoff>>8;
	baseANote=baseAPitch>>8; // 64 semitones
	baseBNote=baseBPitch>>8;
	
	baseCutoff&=0xff;
	
	if(currentPreset.bitParameters&bpChromaticPitch)
	{
		baseAPitch=0;
		baseBPitch=0;
	}
	else
	{
		baseAPitch&=0xff;
		baseBPitch&=0xff;
	}
	
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		if (!assigner_getAssignment(v,&note))
			continue;
		
		cva=satAddU16S32(tuner_computeCVFromNote(baseANote+note,baseAPitch,pcOsc1A+v),(int32_t)p600.benderCVs[pcOsc1A+v]+mTune);
		cvb=satAddU16S32(tuner_computeCVFromNote(baseBNote+note,baseBPitch,pcOsc1B+v),(int32_t)p600.benderCVs[pcOsc1B+v]+mTune+fineBFreq);
		
		if(currentPreset.trackingShift>=0)
			cvf=satAddU16S16(tuner_computeCVFromNote((note>>currentPreset.trackingShift)+baseCutoffNote,baseCutoff,pcFil1+v),p600.benderCVs[pcFil1+v]);
		else
			p600.filterNoteCV[v]=cvf=satAddU16S16(tuner_computeCVFromNote(baseCutoffNote,baseCutoff,pcFil1+v),p600.benderCVs[pcFil1+v]); // no glide if no tracking

		if(p600.gliding)
		{
			if(assigner_getMode()==mMonoLow || assigner_getMode()==mMonoHigh)
			{
				p600.oscATargetCV[P600_MONO_ENV]=cva;
				p600.oscBTargetCV[P600_MONO_ENV]=cvb;
				p600.filterTargetCV[P600_MONO_ENV]=cvf;
			}
			else
			{
				p600.oscATargetCV[v]=cva;
				p600.oscBTargetCV[v]=cvb;
				p600.filterTargetCV[v]=cvf;
			}
		}
		else			
		{
			p600.oscANoteCV[v]=cva;
			p600.oscBNoteCV[v]=cvb;
			p600.filterNoteCV[v]=cvf;
		}
				
	}
}

static void computeBenderCVs(void)
{
	int32_t bend,amt;
	uint16_t pos;
	p600CV_t cv;

	// pot didn't move -> nothing to compute
	
	pos=potmux_getValue(ppPitchWheel);
	
	if(pos==p600.benderRawPosition)
		return;
	
	p600.benderRawPosition=pos;
	
	// compute adjusted bender amount
	
	amt=pos;
	amt+=P600_BENDER_OFFSET;
	
	if(amt<settings.benderMiddle)
	{
		amt=settings.benderMiddle-amt;
		amt*=INT16_MIN;
		amt/=settings.benderMiddle;
	}
	else
	{
		amt-=settings.benderMiddle;
		amt*=INT16_MAX;
		amt/=(UINT16_MAX-settings.benderMiddle+P600_BENDER_OFFSET);
	}
	p600.benderAmount=MIN(MAX(amt,INT16_MIN),INT16_MAX);

	// compute bends
	
	switch(currentPreset.benderTarget)
	{
	case modPitch:
		for(cv=pcOsc1A;cv<=pcOsc6B;++cv)
		{
			bend=tuner_computeCVFromNote(currentPreset.benderSemitones*2,0,cv)-tuner_computeCVFromNote(0,0,cv);
			bend*=p600.benderAmount;
			bend/=UINT16_MAX;
			p600.benderCVs[cv]=bend;
		}
		break;
	case modFilter:
		bend=currentPreset.benderSemitones;
		bend*=p600.benderAmount;
		bend/=12;
		for(cv=pcFil1;cv<=pcFil6;++cv)
			p600.benderCVs[cv]=bend;
		break;
	case modVolume:
		bend=currentPreset.benderSemitones;
		bend*=p600.benderAmount;
		bend/=12;
		p600.benderVolumeCV=bend;
		break;
	default:
		;
	}
}

static inline void computeGlide(uint16_t * out, const uint16_t target, const uint16_t amount)
{
	uint16_t diff;
	
	if(*out<target)
	{
		diff=target-*out;
		*out+=MIN(amount,diff);
	}
	else if(*out>target)
	{
		diff=*out-target;
		*out-=MIN(amount,diff);
	}
}

static void checkFinishedVoices(void)
{
	int8_t i,poly;
	
	poly=assigner_getMode()==mPoly;

	// when amp env finishes, voice is done
	
	for(i=0;i<P600_VOICE_COUNT;++i)
		if (assigner_getAssignment(i,NULL) && adsr_getStage(&p600.ampEnvs[poly?i:P600_MONO_ENV])==sWait)
			assigner_voiceDone(i);
}

static void refreshGates(void)
{
	synth_setGate(pgASaw,(currentPreset.bitParameters&bpASaw)!=0);
	synth_setGate(pgBSaw,(currentPreset.bitParameters&bpBSaw)!=0);
	synth_setGate(pgATri,(currentPreset.bitParameters&bpATri)!=0);
	synth_setGate(pgBTri,(currentPreset.bitParameters&bpBTri)!=0);
	synth_setGate(pgSync,(currentPreset.bitParameters&bpSync)!=0);
	synth_setGate(pgPModFA,(currentPreset.bitParameters&bpPModFA)!=0);
	synth_setGate(pgPModFil,(currentPreset.bitParameters&bpPModFil)!=0);
}


static void refreshAssignerSettings(void)
{
	if((currentPreset.bitParameters&bpUnison)!=0)
		assigner_setMode(currentPreset.assignerMonoMode);
	else
		assigner_setMode(mPoly);
}

static void refreshEnvSettings(int8_t type)
{
	uint8_t expo,shift;
	int8_t i;
	struct adsr_s * a;
		
	expo=(currentPreset.envFlags[type]&ENV_EXPO)!=0;
	shift=(currentPreset.envFlags[type]&ENV_SLOW)?3:1;

	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		if(type)
			a=&p600.filEnvs[i];
		else
			a=&p600.ampEnvs[i];

		adsr_setShape(a,expo);
		adsr_setSpeedShift(a,shift);
	}
}

static void refreshLfoSettings(void)
{
	lfoShape_t shape;

	shape=1+((currentPreset.bitParameters&bpLFOShape)?1:0)+currentPreset.lfoAltShapes*2;

	lfo_setShape(&p600.lfo,shape);
	lfo_setSpeedShift(&p600.lfo,currentPreset.lfoShift*2);
}

static void refreshSevenSeg(void)
{
	if(p600.presetDigit<0)
	{
		sevenSeg_setAscii(' ',' ');
		led_set(plDot,1,0);
	}
	else
	{
		if(p600.presetDigit>0)
		{
			if(p600.presetAwaitingNumber>=0)
				sevenSeg_setAscii('0'+p600.presetAwaitingNumber,' ');
			else
				sevenSeg_setAscii(' ',' ');
		}
		else
		{
			sevenSeg_setNumber(settings.presetNumber);
			led_set(plDot,0,0);
		}
	}
}

static void refreshFullState(void)
{
	refreshGates();
	refreshAssignerSettings();
	refreshLfoSettings();
	refreshEnvSettings(0);
	refreshEnvSettings(1);

	refreshSevenSeg();
}


static void readManualMode(void)
{
	continuousParameter_t cp;
	bitParameter_t bp;
	uint32_t mask;
	
	for(cp=0;cp<cpCount;++cp)	
		currentPreset.continuousParameters[cp]=potmux_getValue(continuousParameterToPot[cp]);

	for(bp=0;bp<sizeof(bitParameterToButton);++bp)	
	{
		mask=(uint32_t)1<<bp;
		
		if(scanner_buttonState(bitParameterToButton[bp]))
			currentPreset.bitParameters|=mask;
		else
			currentPreset.bitParameters&=~mask;
	}
	
	currentPreset.trackingShift=-1;
	if(scanner_buttonState(pbFilFull))
		currentPreset.trackingShift=0;
	if(scanner_buttonState(pbFilHalf))
		currentPreset.trackingShift=1;
	
	currentPreset.lfoTargets=0;
	if(scanner_buttonState(pbLFOFil))
		currentPreset.lfoTargets|=1<<modFilter;
	if(scanner_buttonState(pbLFOFreq))
		currentPreset.lfoTargets|=1<<modPitch;
	if(scanner_buttonState(pbLFOPW))
		currentPreset.lfoTargets|=1<<modPW;
		
	refreshFullState();
}

void p600_init(void)
{
	memset(&p600,0,sizeof(p600));
	
	// defaults
	
	settings.presetNumber=0;
	settings.benderMiddle=UINT16_MAX/2;
	settings.presetMode=-1;
	currentPreset.assignerMonoMode=mUnisonLow;
	currentPreset.benderSemitones=5;
	currentPreset.benderTarget=modPitch;
	currentPreset.envFlags[0]=ENV_EXPO;
	currentPreset.envFlags[1]=ENV_EXPO;
	p600.presetDigit=-1;
	p600.presetAwaitingNumber=-1;
	
	// init
	
	scanner_init();
	display_init();
	synth_init();
	potmux_init();
	tuner_init();
	assigner_init();

	int8_t i;
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_init(&p600.ampEnvs[i]);
		adsr_init(&p600.filEnvs[i]);
	}

	// load stuff from storage
	
	int8_t settingsOk;
	
	settingsOk=settings_load();
	preset_loadCurrent(settings.presetNumber);
	
	// state

	if(!settingsOk)
	{
#ifndef DEBUG
		tuner_tuneSynth();
#endif		
	}
	
	lfo_init(&p600.lfo,tuner_computeCVFromNote(69,42,pcFil1)); // uses tuning, not random, but good enough
	
	scanner_update(1); // set initial state
	
	refreshFullState();
	
	// a nice welcome message ;)
	
	sevenSeg_scrollText("GliGli's P600 upgrade "VERSION,1);
}

void p600_update(void)
{
	int8_t i;
	static uint8_t frc=0;
	int8_t updatingSlow;
	
	// free running counter
	
	++frc;
	
	// toggle tape out (debug)

	BLOCK_INT
	{
		io_write(0x0e,((frc&1)<<2)|0b00110001);
	}

	// which pots do we have to read?
	
	updatingSlow=frc&0x03; // 1/4 of the time, alternatively
	
	switch(updatingSlow)
	{
	case 0:
		potmux_need(ppAmpAtt,ppAmpDec,ppAmpSus,ppAmpRel,ppFilAtt,ppFilDec,ppFilSus,ppFilRel);
		break;
	case 1:
		potmux_need(ppSpeed,ppFilEnvAmt,ppPModFilEnv,ppPModOscB,ppMVol);
		break;
	case 2:
		potmux_need(ppMTune,ppFreqBFine,ppAPW,ppBPW);
		break;
	case 3:
		potmux_need(ppResonance,ppMixer,ppGlide,ppLFOAmt,ppLFOFreq);
		break;
	}

	potmux_need(ppFreqA,ppFreqB,ppPitchWheel,ppModWheel,ppCutoff);
	
	// read them
	
	potmux_update();
	
	// manual mode
	
	if(settings.presetMode<0) // TODO: need to detect pot changes too !
	{
		readManualMode();
	}

	// update CVs

	switch(updatingSlow)
	{
	case 0:
		for(i=0;i<P600_VOICE_COUNT;++i)
		{
			adsr_setCVs(&p600.ampEnvs[i],
					 currentPreset.continuousParameters[cpAmpAtt],
					 currentPreset.continuousParameters[cpAmpDec],
					 currentPreset.continuousParameters[cpAmpSus],
					 currentPreset.continuousParameters[cpAmpRel],
					 UINT16_MAX);
			adsr_setCVs(&p600.filEnvs[i],
					 currentPreset.continuousParameters[cpFilAtt],
					 currentPreset.continuousParameters[cpFilDec],
					 currentPreset.continuousParameters[cpFilSus],
					 currentPreset.continuousParameters[cpFilRel],
					 UINT16_MAX);
		}
		break;
	case 1:
		synth_setCV(pcPModOscB,currentPreset.continuousParameters[cpPModOscB],1,1);
		synth_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),p600.benderVolumeCV),1,1);

		currentPreset.continuousParameters[cpGlide]=(UINT16_MAX-currentPreset.continuousParameters[cpGlide])>>5; // 11bit glide
		p600.gliding=currentPreset.continuousParameters[cpGlide]<2000;
		break;
	case 2:
		if(!(currentPreset.lfoTargets&(1<<modPW)))
		{
			uint16_t pa,pb;

			pa=pb=UINT16_MAX;

			if((currentPreset.bitParameters&bpASqr)!=0)
				pa=currentPreset.continuousParameters[cpAPW];

			if((currentPreset.bitParameters&bpBSqr)!=0)
				pb=currentPreset.continuousParameters[cpBPW];

			synth_setCV(pcAPW,pa,1,1);
			synth_setCV(pcBPW,pb,1,1);
		}
		break;
	case 3:
		synth_setCV(pcVolA,currentPreset.continuousParameters[cpVolA],1,1);
		synth_setCV(pcVolB,currentPreset.continuousParameters[cpVolB],1,1);
		synth_setCV(pcResonance,currentPreset.continuousParameters[cpResonance],1,1);
		break;
	}

	computeBenderCVs();
	computeTunedCVs();
	lfo_setCVs(&p600.lfo,
			currentPreset.continuousParameters[cpLFOFreq],
			satAddU16U16(currentPreset.continuousParameters[cpLFOAmt],
				potmux_getValue(ppModWheel)>>currentPreset.modwheelShift));
}

void p600_fastInterrupt(void)
{
	int32_t va,vb,vf;
	uint16_t envVal;
	int16_t pitchLfoVal,filterLfoVal,filEnvAmt,oscEnvAmt;
	int8_t assigned,hz500,hz63,polyMul,monoGlidingMul,envVoice,pitchVoice;
	int8_t silentVoice[P600_VOICE_COUNT];

	static uint8_t frc=0;
	
	// slower updates
	
	hz500=(frc&0x03)==0; // 1/4 of the time (500hz)
	hz63=(frc&0x01f)==0; // 1/32 of the time (62.5hz)

	if(hz500)
	{
		if(p600.gliding)
			for(int8_t v=0;v<P600_VOICE_COUNT;++v)
			{
				computeGlide(&p600.oscANoteCV[v],p600.oscATargetCV[v],currentPreset.continuousParameters[cpGlide]);
				computeGlide(&p600.oscBNoteCV[v],p600.oscBTargetCV[v],currentPreset.continuousParameters[cpGlide]);
				computeGlide(&p600.filterNoteCV[v],p600.filterTargetCV[v],currentPreset.continuousParameters[cpGlide]);
			}

		
		scanner_update(hz63);
		display_update(hz63);
	}
	
	++frc;

	// lfo
	
	lfo_update(&p600.lfo);
	
	pitchLfoVal=filterLfoVal=0;
	
	if(currentPreset.lfoTargets&(1<<modPitch))
		pitchLfoVal=p600.lfo.output;

	if(currentPreset.lfoTargets&(1<<modFilter))
		filterLfoVal=p600.lfo.output;
	
	if(currentPreset.lfoTargets&(1<<modPW))
	{
		va=vb=p600.lfo.output;

		va+=currentPreset.continuousParameters[cpAPW];
		if((currentPreset.bitParameters&bpASqr)==0)
			va=UINT16_MAX;
		synth_setCV32Sat(pcAPW,va,1,0);
		

		vb+=currentPreset.continuousParameters[cpBPW];
		if((currentPreset.bitParameters&bpBSqr)==0)
			vb=UINT16_MAX;
		synth_setCV32Sat(pcBPW,vb,1,0);
	}

	// global env computations
	
	vf=currentPreset.continuousParameters[cpFilEnvAmt];
	vf+=INT16_MIN;
	filEnvAmt=vf;
	
	oscEnvAmt=0;
	if((currentPreset.bitParameters&bpPModFA)!=0)
	{
		va=currentPreset.continuousParameters[cpPModFilEnv];
		va+=INT16_MIN;
		oscEnvAmt=va;		
	}
	
	// per voice stuff
	
	polyMul=(assigner_getMode()==mPoly)?1:0;
	monoGlidingMul=((assigner_getMode()==mMonoLow || assigner_getMode()==mMonoHigh) && p600.gliding)?0:1;
	
	if(!polyMul)
	{
		// handle mono modes env update
		adsr_update(&p600.filEnvs[P600_MONO_ENV]);
		adsr_update(&p600.ampEnvs[P600_MONO_ENV]);
	}

#ifdef UNROLL_VOICES	
		// declare a nested function to unroll per-voice updates
	inline void unrollVoices(int8_t v)
#else
	for(int8_t v=0;v<P600_VOICE_COUNT;++v)
#endif
	{
		assigned=assigner_getAssignment(v,NULL);
		
		silentVoice[v]=!assigned;
		
		if(assigned)
		{
			if(polyMul)
			{
				// handle envs update
				adsr_update(&p600.filEnvs[v]);
				adsr_update(&p600.ampEnvs[v]);
			}
		
			envVoice=v*polyMul;
			pitchVoice=v*monoGlidingMul;

			// compute CVs & apply them
			
			envVal=p600.filEnvs[envVoice].output;
			
			va=vb=pitchLfoVal;

			va+=scaleU16S16(envVal,oscEnvAmt);	
			va+=p600.oscANoteCV[pitchVoice];
			
			synth_setCV32Sat(pcOsc1A+v,va,1,0);

			vb+=p600.oscBNoteCV[pitchVoice];
			synth_setCV32Sat(pcOsc1B+v,vb,1,0);

			vf=filterLfoVal;
			vf+=scaleU16S16(envVal,filEnvAmt);
			vf+=p600.filterNoteCV[pitchVoice];
			synth_setCV32Sat(pcFil1+v,vf,1,0);
			
			synth_setCV(pcAmp1+v,p600.ampEnvs[envVoice].output,1,0);
		}
	}
	
#ifdef UNROLL_VOICES	
		// P600_VOICE_COUNT calls
	unrollVoices(0);
	unrollVoices(1);
	unrollVoices(2);
	unrollVoices(3);
	unrollVoices(4);
	unrollVoices(5);
#endif
	
	// handle voices that are done playing
	
	if(hz63)
	{
		checkFinishedVoices();

		for(int8_t v=0;v<P600_VOICE_COUNT;++v)
			if(silentVoice[v])
				synth_setCV(pcAmp1+v,0,1,1);
	}
}

void p600_slowInterrupt(void)
{
}

void p600_buttonEvent(p600Button_t button, int pressed)
{
	// tuning

	if(!pressed && button==pbTune)
	{
		tuner_tuneSynth();

		// tuner will thrash state
		refreshFullState();
	}

	// preset mode
	
	if(pressed && button==pbPreset)
	{
		settings.presetMode=((settings.presetMode+2)%2)-1; //TODO: second preset page, how to store?
		
		led_set(plPreset,settings.presetMode>=0,settings.presetMode>=1);
		
		settings_save();		
		
		if(settings.presetMode>=0)
		{
			preset_loadCurrent(settings.presetNumber);
			p600.presetDigit=0;
			
			refreshFullState();
		}
		else
		{
			p600.presetDigit=-1;
		}

		refreshSevenSeg();
	}
	
	if(pressed && button==pbRecord)
	{
		if(p600.presetDigit==1)
		{
			p600.presetDigit=(settings.presetMode>=0)?0:-1;
			led_set(plRecord,0,0);
		}
		else
		{
			p600.presetDigit=1;
			led_set(plRecord,1,1);
		}
		
		refreshSevenSeg();
	}
	
	if(p600.presetDigit>=0)
	{
		if(pressed && button>=pb0 && button<=pb9)
		{
			switch(p600.presetDigit)
			{
			case 0:
			case 1:
				p600.presetAwaitingNumber=button-pb0;
				p600.presetDigit+=2;
				break;
			case 2:
			case 3:
				p600.presetAwaitingNumber=p600.presetAwaitingNumber*10+(button-pb0);

				if(p600.presetDigit==3) // store?
				{
					preset_saveCurrent(p600.presetAwaitingNumber);
					led_set(plRecord,0,0);
				}

				if(preset_loadCurrent(p600.presetAwaitingNumber))
				{
					settings.presetNumber=p600.presetAwaitingNumber;
					settings_save();		
	
					refreshFullState();
				}

				p600.presetAwaitingNumber=-1;
				p600.presetDigit=(settings.presetMode>=0)?0:-1;
				break;
			}

			refreshSevenSeg();
		}
	}
	else
	{
		// assigner

		if((pressed && button==pb0))
		{
			currentPreset.assignerMonoMode=(currentPreset.assignerMonoMode%mMonoHigh)+1;

			refreshAssignerSettings();
			sevenSeg_scrollText(assigner_modeName(currentPreset.assignerMonoMode),1);
		}

		if(button==pbUnison)
		{
			readManualMode();
			
			refreshAssignerSettings();
			sevenSeg_scrollText(assigner_modeName(assigner_getMode()),1);
		}

		// lfo

		if((pressed && (button>=pb1 && button<=pb2)) || button==pbLFOShape)
		{
			const char * s=NULL;

			if(button==pb1 || button==pbLFOShape)
			{
				if(button==pb1)
					currentPreset.lfoAltShapes=1-currentPreset.lfoAltShapes;

				s=lfo_shapeName(1+((currentPreset.bitParameters&bpLFOShape)!=0)+currentPreset.lfoAltShapes*2);
			}

			if(button==pb2)
			{
				currentPreset.lfoShift=(currentPreset.lfoShift+1)%3;

				switch(currentPreset.lfoShift)
				{
				case 0:
					s="Slo";
					break;
				case 1:
					s="Med";
					break;
				case 2:
					s="Fast";
					break;
				}
			}

			refreshLfoSettings();

			sevenSeg_scrollText(s,1);
		}

		// modwheel

		if((pressed && button==pb3))
		{
			const char * s=NULL;

			currentPreset.modwheelShift=(currentPreset.modwheelShift+2)%6;

			switch(currentPreset.modwheelShift)
			{
			case 0:
				s="Full";
				break;
			case 2:
				s="Half";
				break;
			case 4:
				s="Min";
				break;
			}

			sevenSeg_scrollText(s,1);
		}


		// envs

		if(pressed && (button>=pb4 && button<=pb5))
		{
			char s[20]="";
			uint8_t type;

			type=(button==pb5)?1:0;

			currentPreset.envFlags[type]=(currentPreset.envFlags[type]+1)%4;

			refreshEnvSettings(type);


			switch(currentPreset.envFlags[type])
			{
			case 0:
				strcat(s,"fast lin");
				break;
			case 1:
				strcat(s,"fast exp");
				break;
			case 2:
				strcat(s,"slo lin");
				break;
			case 3:
				strcat(s,"slo exp");
				break;
			}

			if(type)
				strcat(s," fil");
			else
				strcat(s," amp");

			sevenSeg_scrollText(s,1);
		}

		// pitch mode

		if(pressed && button==pb6)
		{
			const char * s=NULL;

			currentPreset.bitParameters^=bpChromaticPitch;

			if(currentPreset.bitParameters&bpChromaticPitch)
				s="Chromatic";
			else
				s="Free";

			sevenSeg_scrollText(s,1);
		}


		// bender

		if(pressed && (button>=pb7 && button<=pb9))
		{
			const char * s=NULL;

			if(button==pb7)
			{
				switch(currentPreset.benderSemitones)
				{
				case 3:
					currentPreset.benderSemitones=5;
					s="5th";
					break;
				case 5:
					currentPreset.benderSemitones=12;
					s="Oct";
					break;
				case 12:
					currentPreset.benderSemitones=3;
					s="3rd";
					break;
				}
			}

			if(button==pb8)
			{
				currentPreset.benderTarget=(currentPreset.benderTarget+1)%(modVolume+1);
				s=modulationName(currentPreset.benderTarget);
			}

			if(button==pb9)
			{
				settings.benderMiddle=satAddU16S16(potmux_getValue(ppPitchWheel),P600_BENDER_OFFSET);
				s="Calibrated";
			}

			// clear bender CVs, force recompute
			memset(&p600.benderCVs,0,sizeof(p600.benderCVs));
			p600.benderRawPosition=~p600.benderRawPosition;

			sevenSeg_scrollText(s,1);
		}
	}
}

void p600_keyEvent(uint8_t key, int pressed)
{
	assigner_assignNote(key,pressed);
}

void p600_assignerEvent(uint8_t note, int8_t gate, int8_t voice)
{
	int8_t env;
	
	computeTunedCVs();

	env=voice;
	if(assigner_getMode()!=mPoly)
		env=P600_MONO_ENV;

	adsr_setGate(&p600.filEnvs[env],gate);
	adsr_setGate(&p600.ampEnvs[env],gate);
	
#ifdef DEBUG		
	print("assign note ");
	phex(note);
	print("  gate ");
	phex(gate);
	print(" voice ");
	phex(voice);
	print("\n");
#endif
}