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

#define UNROLL_VOICES // undefine this to save code size, at the expense of speed

#define P600_MONO_ENV 0 // informative constant, don't change it!
#define P600_BENDER_OFFSET -16384

#define ENV_EXPO 1
#define ENV_SLOW 2

typedef enum
{
	modOff=0,modPitch=1,modFilter=2,modVolume=3,modPW=4,modResonance=5,modMixer=6
} modulation_t;

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


	uint8_t envFlags[2]; // 0:amp / 1:fil
	
	int8_t trackingShift;

	assignerMode_t assignerMonoMode;
	
	int8_t lfoAltShapes;
	modulation_t lfoTargets;
	uint8_t lfoShift;
	
	int8_t modwheelShift;
	
	uint16_t benderMiddle;
	int16_t benderRawPosition;
	int16_t benderAmount;
	int16_t benderCVs[pcFil6-pcOsc1A+1];
	int16_t benderVolumeCV;
	int8_t benderSemitones;
	modulation_t benderTarget;
	
	uint16_t glideAmount;
	int8_t gliding;
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

static void adjustTunedCVs(void)
{
	int32_t baseCutoff;
	uint16_t cva,cvb,cvf;
	int16_t mTune,fineBFreq;
	uint8_t note,baseANote,baseBNote;
	int8_t v;
	
	// filters and oscs
	
	mTune=(potmux_getValue(ppMTune)>>8)+INT8_MIN;
	fineBFreq=(potmux_getValue(ppFreqBFine)>>7)+INT8_MIN*2;
	
	baseCutoff=potmux_getValue(ppCutoff);
	
	baseANote=potmux_getValue(ppFreqA)>>10; // 64 semitones
	baseBNote=potmux_getValue(ppFreqB)>>10;
	
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		if (!assigner_getAssignment(v,&note))
			continue;
		
		cva=satAddU16S32(tuner_computeCVFromNote(baseANote+note,pcOsc1A+v),(int32_t)p600.benderCVs[pcOsc1A+v]+mTune);
		cvb=satAddU16S32(tuner_computeCVFromNote(baseBNote+note,pcOsc1B+v),(int32_t)p600.benderCVs[pcOsc1B+v]+mTune+fineBFreq);
		
		if(p600.trackingShift>=0)
			cvf=satAddU16S32(tuner_computeCVFromNote(note,pcFil1+v)>>p600.trackingShift,(int32_t)p600.benderCVs[pcFil1+v]+baseCutoff);
		else
			p600.filterNoteCV[v]=cvf=satAddU16S32(tuner_computeCVFromNote(0,pcFil1+v),(int32_t)p600.benderCVs[pcFil1+v]+baseCutoff); // no glide if no tracking

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
	
	if(amt<p600.benderMiddle)
	{
		amt=p600.benderMiddle-amt;
		amt*=INT16_MIN;
		amt/=p600.benderMiddle;
	}
	else
	{
		amt-=p600.benderMiddle;
		amt*=INT16_MAX;
		amt/=(UINT16_MAX-p600.benderMiddle+P600_BENDER_OFFSET);
	}
	p600.benderAmount=MIN(MAX(amt,INT16_MIN),INT16_MAX);

	// compute bends
	
	switch(p600.benderTarget)
	{
	case modPitch:
		for(cv=pcOsc1A;cv<=pcOsc6B;++cv)
		{
			bend=tuner_computeCVFromNote(p600.benderSemitones*2,cv)-tuner_computeCVFromNote(0,cv);
			bend*=p600.benderAmount;
			bend/=UINT16_MAX;
			p600.benderCVs[cv]=bend;
		}
		break;
	case modFilter:
		bend=p600.benderSemitones;
		bend*=p600.benderAmount;
		bend/=12;
		for(cv=pcFil1;cv<=pcFil6;++cv)
			p600.benderCVs[cv]=bend;
		break;
	case modVolume:
		bend=p600.benderSemitones;
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
	synth_setGate(pgASaw,scanner_buttonState(pbASaw));
	synth_setGate(pgBSaw,scanner_buttonState(pbBSaw));
	synth_setGate(pgATri,scanner_buttonState(pbATri));
	synth_setGate(pgBTri,scanner_buttonState(pbBTri));
	synth_setGate(pgSync,scanner_buttonState(pbSync));
	synth_setGate(pgPModFA,scanner_buttonState(pbPModFA));
	synth_setGate(pgPModFil,scanner_buttonState(pbPModFil));

	p600.trackingShift=-1;
	if(scanner_buttonState(pbFilFull))
		p600.trackingShift=0;
	if(scanner_buttonState(pbFilHalf))
		p600.trackingShift=1;
	
	p600.lfoTargets=0;
	if(scanner_buttonState(pbLFOFil))
		p600.lfoTargets|=1<<modFilter;
	if(scanner_buttonState(pbLFOFreq))
		p600.lfoTargets|=1<<modPitch;
	if(scanner_buttonState(pbLFOPW))
		p600.lfoTargets|=1<<modPW;
}


static void refreshAssignerSettings(void)
{
	if(scanner_buttonState(pbUnison))
		assigner_setMode(p600.assignerMonoMode);
	else
		assigner_setMode(mPoly);
}

static void refreshEnvSettings(int8_t type)
{
	uint8_t expo,shift;
	int8_t i;
	struct adsr_s * a;
		
	expo=(p600.envFlags[type]&ENV_EXPO)!=0;
	shift=(p600.envFlags[type]&ENV_SLOW)?3:1;

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

	shape=1+scanner_buttonState(pbLFOShape)+p600.lfoAltShapes*2;

	lfo_setShape(&p600.lfo,shape);
	lfo_setSpeedShift(&p600.lfo,p600.lfoShift*2);
}

static void refreshFullState(void)
{
	refreshGates();
	refreshAssignerSettings();
	refreshLfoSettings();
	refreshEnvSettings(0);
	refreshEnvSettings(1);

	// clear message spam
	sevenSeg_scrollText(NULL,0);
}

void p600_init(void)
{
	memset(&p600,0,sizeof(p600));
	
	// defaults
	
	p600.assignerMonoMode=mUnisonLow;
	p600.benderMiddle=UINT16_MAX/2;
	p600.benderSemitones=5;
	p600.benderTarget=modPitch;
	p600.envFlags[0]=ENV_EXPO;
	p600.envFlags[1]=ENV_EXPO;
	
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

#if 0
	{
		int16_t i;
		uint16_t e,p;
		int8_t lower,pot;
		uint32_t nr=0;
		static uint16_t exc[42];
		
		for(i=0;i<sizeof(exc);++i)
			exc[i]=rand();

		for(;;)
		{
			//prepare
			
			for(i=0;i<sizeof(exc);++i)
			{
				// write
				
				++exc[i];
				
				mem_write(0x2000+i,exc[i]);
				mem_write(0x2080+i,exc[i]>>8);
			}

			for(i=0;i<sizeof(exc);++i)
			{
				// read back
				
				e=((uint16_t)mem_read(0x2080+i))<<8;
				e|=mem_read(0x2000+i);
				
				if(e!=exc[i])
				{
					print("m");phex16(e);phex16(exc[i]);
					++nr;
				}
			}

			pot=rand()%32;
			potmux_need(pot);
			potmux_update();
			p=potmux_getValue(pot);

			for(i=0;i<sizeof(exc);++i)
			{
				// test potmux & dac
				
				io_write(0x0a,(pot&0x0f)|(0x20>>(pot>>4)));

				e=exc[i];

				dac_write(e);				

				// is DAC value lower than pot value?
				lower=(io_read(0x09)&0x08)!=0;

				if(lower!=(e<p))
				{
					print("d ");phex16(e);print(" ");phex16(p);print("     ");
					++nr;
				}
			}

			io_write(0x0a,0xff);
			CYCLE_WAIT(8);

			sevenSeg_setNumber(nr);
			display_update(1);
		}
	}
#endif	

#if 0
	synth_update();
	for(;;)
	{
		potmux_need(ppMTune);
		potmux_need(ppMVol,ppPitchWheel,ppModWheel,ppMixer);
		potmux_update();
		if(scanner_buttonState(pb1))
			sevenSeg_setNumber((int32_t)potmux_getValue(ppMVol)>>8);
		else if(scanner_buttonState(pb2))
			sevenSeg_setNumber((int32_t)potmux_getValue(ppPitchWheel)>>8);
		else if(scanner_buttonState(pb3))
			sevenSeg_setNumber((int32_t)potmux_getValue(ppModWheel)>>8);
		else if(scanner_buttonState(pb4))
			sevenSeg_setNumber((int32_t)potmux_getValue(ppMixer)>>8);
		else
			sevenSeg_setNumber((int32_t)potmux_getValue(ppMTune)>>8);
		scanner_update(1);
		display_update(1);
	}
#endif
	
	// state
	
#ifndef DEBUG		
	tuner_tuneSynth();
#endif
	
	lfo_init(&p600.lfo,tuner_computeCVFromNote(69,pcFil1)); // uses tuning, not random, but good enough
	
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
		potmux_need(ppFreqA,ppFreqB,ppMTune,ppFreqBFine,ppAPW,ppBPW);
		break;
	case 3:
		potmux_need(ppResonance,ppMixer,ppGlide,ppLFOAmt,ppLFOFreq);
		break;
	}

	potmux_need(ppPitchWheel,ppModWheel,ppCutoff);
	
	// read them
	
	potmux_update();

	// update CVs

	switch(updatingSlow)
	{
	case 0:
		for(i=0;i<P600_VOICE_COUNT;++i)
		{
			adsr_setCVs(&p600.ampEnvs[i],potmux_getValue(ppAmpAtt),potmux_getValue(ppAmpDec),potmux_getValue(ppAmpSus),potmux_getValue(ppAmpRel),UINT16_MAX);
			adsr_setCVs(&p600.filEnvs[i],potmux_getValue(ppFilAtt),potmux_getValue(ppFilDec),potmux_getValue(ppFilSus),potmux_getValue(ppFilRel),UINT16_MAX);
		}
		break;
	case 1:
		synth_setCV(pcPModOscB,potmux_getValue(ppPModOscB),1,1);
		synth_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),p600.benderVolumeCV),1,1);

		p600.glideAmount=(UINT16_MAX-potmux_getValue(ppSpeed))>>5; // 11bit glide
		p600.gliding=p600.glideAmount<2000;
		break;
	case 2:
		if(!(p600.lfoTargets&(1<<modPW)))
		{
			uint16_t pa,pb;

			pa=pb=UINT16_MAX;

			if(scanner_buttonState(pbASqr))
				pa=potmux_getValue(ppAPW);

			if(scanner_buttonState(pbBSqr))
				pb=potmux_getValue(ppBPW);

			synth_setCV(pcAPW,pa,1,1);
			synth_setCV(pcBPW,pb,1,1);
		}
		break;
	case 3:
		synth_setCV(pcVolA,potmux_getValue(ppMixer),1,1);
		synth_setCV(pcVolB,potmux_getValue(ppGlide),1,1);
		synth_setCV(pcResonance,potmux_getValue(ppResonance),1,1);
		break;
	}

	computeBenderCVs();
	adjustTunedCVs();
	lfo_setCVs(&p600.lfo,potmux_getValue(ppLFOFreq),satAddU16U16(potmux_getValue(ppLFOAmt),potmux_getValue(ppModWheel)>>p600.modwheelShift));
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
				computeGlide(&p600.oscANoteCV[v],p600.oscATargetCV[v],p600.glideAmount);
				computeGlide(&p600.oscBNoteCV[v],p600.oscBTargetCV[v],p600.glideAmount);
				computeGlide(&p600.filterNoteCV[v],p600.filterTargetCV[v],p600.glideAmount);
			}

		
		scanner_update(hz63);
		display_update(hz63);
	}
	
	++frc;

	// lfo
	
	lfo_update(&p600.lfo);
	
	pitchLfoVal=filterLfoVal=0;
	
	if(p600.lfoTargets&(1<<modPitch))
		pitchLfoVal=p600.lfo.output;

	if(p600.lfoTargets&(1<<modFilter))
		filterLfoVal=p600.lfo.output;
	
	if(p600.lfoTargets&(1<<modPW))
	{
		va=vb=p600.lfo.output;

		va+=potmux_getValue(ppAPW);
		if (!scanner_buttonState(pbASqr))
			va=UINT16_MAX;
		synth_setCV32Sat(pcAPW,va,1,0);
		

		vb+=potmux_getValue(ppBPW);
		if (!scanner_buttonState(pbBSqr))
			vb=UINT16_MAX;
		synth_setCV32Sat(pcBPW,vb,1,0);
	}

	// global env computations
	
	vf=potmux_getValue(ppFilEnvAmt);
	vf+=INT16_MIN;
	filEnvAmt=vf;
	
	oscEnvAmt=0;
	if(scanner_buttonState(pbPModFA))
	{
		va=potmux_getValue(ppPModFilEnv);
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
	
	CYCLE_WAIT(40); // 10 us to let VCAs properly follow envelopes
	
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
	refreshGates();
	
	// tuning
	
	if(!pressed && button==pbTune)
	{
		tuner_tuneSynth();
		
		// tuner will thrash state
		refreshFullState();
	}
	
	// assigner

	if((pressed && button==pb0))
	{
		p600.assignerMonoMode=(p600.assignerMonoMode%mMonoHigh)+1;

		refreshAssignerSettings();
		sevenSeg_scrollText(assigner_modeName(p600.assignerMonoMode),1);
	}

	if(button==pbUnison)
	{
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
				p600.lfoAltShapes=1-p600.lfoAltShapes;
			
			s=lfo_shapeName(1+scanner_buttonState(pbLFOShape)+p600.lfoAltShapes*2);
		}

		if(button==pb2)
		{
			p600.lfoShift=(p600.lfoShift+1)%3;

			switch(p600.lfoShift)
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

		p600.modwheelShift=(p600.modwheelShift+2)%6;

		switch(p600.modwheelShift)
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
		
		p600.envFlags[type]=(p600.envFlags[type]+1)%4;
		
		refreshEnvSettings(type);
		
	
		switch(p600.envFlags[type])
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
	
	// bender
	
	if(pressed && (button>=pb7 && button<=pb9))
	{
		const char * s=NULL;
		
		if(button==pb7)
		{
			switch(p600.benderSemitones)
			{
			case 3:
				p600.benderSemitones=5;
				s="5th";
				break;
			case 5:
				p600.benderSemitones=12;
				s="Oct";
				break;
			case 12:
				p600.benderSemitones=3;
				s="3rd";
				break;
			}
		}
		
		if(button==pb8)
		{
			p600.benderTarget=(p600.benderTarget+1)%(modVolume+1);
			s=modulationName(p600.benderTarget);
		}

		if(button==pb9)
		{
			p600.benderMiddle=satAddU16S16(potmux_getValue(ppPitchWheel),P600_BENDER_OFFSET);
			s="Calibrated";
		}
		
		// clear bender CVs, force recompute
		memset(&p600.benderCVs,0,sizeof(p600.benderCVs));
		p600.benderRawPosition=~p600.benderRawPosition;

		sevenSeg_scrollText(s,1);
	}
	
}

void p600_keyEvent(uint8_t key, int pressed)
{
	assigner_assignNote(key,pressed);
}

void p600_assignerEvent(uint8_t note, int8_t gate, int8_t voice)
{
	int8_t env;
	
	adjustTunedCVs();

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