////////////////////////////////////////////////////////////////////////////////
// Top level code
////////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "../xnormidi/midi_device.h"
#include "../xnormidi/midi.h"

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
#include "uart_6850.h"

#define P600_MONO_ENV 0 // informative constant, don't change it!
#define P600_BENDER_OFFSET -16384

#define ENV_EXPO 1
#define ENV_SLOW 2

#define MIDI_BASE_SWITCH_CC 40
#define MIDI_BASE_COARSE_CC 64
#define MIDI_BASE_FINE_CC 96
#define MIDI_BASE_NOTE 12

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

	MidiDevice midi;

	int16_t benderRawPosition;
	int16_t benderAmount;
	int16_t benderCVs[pcFil6-pcOsc1A+1];
	int16_t benderVolumeCV;

	int16_t glideAmount;
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
	uint16_t cva,cvb,cvf,baseAPitch,baseBPitch,baseCutoff;
	int16_t mTune,fineBFreq;
	uint8_t note,baseCutoffNote,baseANote,baseBNote,trackingNote;
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
		
		// oscs
		
		cva=satAddU16S32(tuner_computeCVFromNote(baseANote+note,baseAPitch,pcOsc1A+v),(int32_t)p600.benderCVs[pcOsc1A+v]+mTune);
		cvb=satAddU16S32(tuner_computeCVFromNote(baseBNote+note,baseBPitch,pcOsc1B+v),(int32_t)p600.benderCVs[pcOsc1B+v]+mTune+fineBFreq);
		
		// filter
		
		trackingNote=baseCutoffNote;
		if(currentPreset.trackingShift>=0)
			trackingNote+=note>>currentPreset.trackingShift;
			
		cvf=satAddU16S16(tuner_computeCVFromNote(trackingNote,baseCutoff,pcFil1+v),p600.benderCVs[pcFil1+v]);
		
		if(currentPreset.trackingShift<0)
			p600.filterNoteCV[v]=cvf; // no glide if no tracking

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

static void handleFinishedVoices(void)
{
	int8_t v,poly;
	
	poly=assigner_getMode()==mPoly;

	// when amp env finishes, voice is done
	
	for(v=0;v<P600_VOICE_COUNT;++v)
		if (assigner_getAssignment(v,NULL))
		{
			if(adsr_getStage(&p600.ampEnvs[poly?v:P600_MONO_ENV])==sWait)
				assigner_voiceDone(v);
		}
		else
		{
			synth_setCV(pcAmp1+v,0,SYNTH_FLAG_IMMEDIATE);
		}	
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


static inline void refreshPulseWidth(int8_t pwm)
{
	int32_t pa,pb;
	
	pa=pb=0;

	int8_t sqrA=currentPreset.bitParameters&bpASqr;
	int8_t sqrB=currentPreset.bitParameters&bpBSqr;

	if(sqrA)
		pa=currentPreset.continuousParameters[cpAPW];

	if(sqrB)
		pb=currentPreset.continuousParameters[cpBPW];

	if(pwm)
	{
		if(sqrA)
			pa+=p600.lfo.output;

		if(sqrB)
			pb+=p600.lfo.output;
		
		BLOCK_INT
		{
			synth_setCV32Sat_FastPath(pcAPW,pa);
			synth_setCV32Sat_FastPath(pcBPW,pb);
		}
	}
	else
	{
		synth_setCV32Sat(pcAPW,pa,SYNTH_FLAG_IMMEDIATE);
		synth_setCV32Sat(pcBPW,pb,SYNTH_FLAG_IMMEDIATE);
	}	
}

static void refreshAssignerSettings(void)
{
	if((currentPreset.bitParameters&bpUnison)!=0)
		assigner_setMode(currentPreset.assignerMonoMode);
	else
		assigner_setMode(mPoly);
}

static void refreshEnvSettings(int8_t type, int8_t display)
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
	
	if(display)
	{
		char s[20]="";
		
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
}

static void refreshLfoSettings(int8_t dispShape,int8_t dispSpd)
{
	const char * s=NULL;
	lfoShape_t shape;

	shape=1+((currentPreset.bitParameters&bpLFOShape)?1:0)+currentPreset.lfoAltShapes*2;

	lfo_setShape(&p600.lfo,shape);
	lfo_setSpeedShift(&p600.lfo,currentPreset.lfoShift*2);

	if(dispShape)
	{
		s=lfo_shapeName(shape);
		sevenSeg_scrollText(s,1);
	}
	else if(dispSpd)
	{
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
		sevenSeg_scrollText(s,1);
	}
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
	refreshLfoSettings(0,0);
	refreshEnvSettings(0,0);
	refreshEnvSettings(1,0);

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
}

static FORCEINLINE void updateVoice(int8_t v,int16_t oscEnvAmt,int16_t filEnvAmt,int16_t pitchLfoVal,int16_t filterLfoVal,int8_t monoGlidingMask,int8_t poly)
{
	int32_t va,vb,vf;
	uint16_t envVal;
	int8_t assigned,envVoice,pitchVoice;
	
	assigned=assigner_getAssignment(v,NULL);

	if(assigned)
	{
		envVoice=P600_MONO_ENV;

		if(poly)
		{
			// handle envs update
			adsr_update(&p600.filEnvs[v]);
			adsr_update(&p600.ampEnvs[v]);

			envVoice=v;
		}

		pitchVoice=v&monoGlidingMask;

		// compute CVs & apply them

		BLOCK_INT
		{
			envVal=p600.filEnvs[envVoice].output;

			va=vb=pitchLfoVal;

			va+=scaleU16S16(envVal,oscEnvAmt);	
			va+=p600.oscANoteCV[pitchVoice];

			synth_setCV32Sat_FastPath(pcOsc1A+v,va);

			vb+=p600.oscBNoteCV[pitchVoice];
			synth_setCV32Sat_FastPath(pcOsc1B+v,vb);

			vf=filterLfoVal;
			vf+=scaleU16S16(envVal,filEnvAmt);
			vf+=p600.filterNoteCV[pitchVoice];
			synth_setCV32Sat_FastPath(pcFil1+v,vf);

			synth_setCV_FastPath(pcAmp1+v,p600.ampEnvs[envVoice].output);
		}
	}
}

static int8_t midiFilterChannel(uint8_t channel)
{
	return settings.midiReceiveChannel<0 || (channel&MIDI_CHANMASK)==settings.midiReceiveChannel;
}

void midi_noteOnEvent(MidiDevice * device, uint8_t channel, uint8_t note, uint8_t velocity)
{
	int16_t intNote;
	
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi note on  ");
	phex(note);
	print("\n");
#endif

	intNote=note-MIDI_BASE_NOTE;
	intNote=MAX(0,intNote);
	
	assigner_assignNote(intNote,velocity!=0);
}

void midi_noteOffEvent(MidiDevice * device, uint8_t channel, uint8_t note, uint8_t velocity)
{
	int16_t intNote;
	
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi note off ");
	phex(note);
	print("\n");
#endif

	intNote=note-MIDI_BASE_NOTE;
	intNote=MAX(0,intNote);
	
	assigner_assignNote(intNote,0);
}

void midi_ccEvent(MidiDevice * device, uint8_t channel, uint8_t control, uint8_t value)
{
	int16_t param;
	
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi cc ");
	phex(control);
	print(" value ");
	phex(value);
	print("\n");
#endif
	
	if(control>=MIDI_BASE_COARSE_CC && control<MIDI_BASE_COARSE_CC+cpCount)
	{
		param=control-MIDI_BASE_COARSE_CC;

		currentPreset.continuousParameters[param]&=0x01fc;
		currentPreset.continuousParameters[param]|=(uint16_t)value<<9;
	}
	else if(control>=MIDI_BASE_FINE_CC && control<MIDI_BASE_FINE_CC+cpCount)
	{
		param=control-MIDI_BASE_FINE_CC;

		currentPreset.continuousParameters[param]&=0xfe00;
		currentPreset.continuousParameters[param]|=(uint16_t)value<<2;
	}
	else if(control>=MIDI_BASE_SWITCH_CC && control<MIDI_BASE_COARSE_CC)
	{
		param=control-MIDI_BASE_SWITCH_CC;
		
		if(value)
			currentPreset.bitParameters|=(uint32_t)1<<param;
		else
			currentPreset.bitParameters&=~((uint32_t)1<<param);
	}
}

void p600_init(void)
{
	memset(&p600,0,sizeof(p600));
	
	// defaults
	
	settings.presetNumber=0;
	settings.benderMiddle=UINT16_MAX/2;
	settings.presetMode=-1;
	settings.midiReceiveChannel=-1;
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
	uart_init();
	
	midi_device_init(&p600.midi);
	midi_register_noteon_callback(&p600.midi,midi_noteOnEvent);
	midi_register_noteoff_callback(&p600.midi,midi_noteOffEvent);
	midi_register_cc_callback(&p600.midi,midi_ccEvent);
	
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
	
	// toggle tape out (debug)

	BLOCK_INT
	{
		++frc;
		io_write(0x0e,((frc&1)<<2)|0b00110001);
	}

	// manual mode
	
	if(settings.presetMode<0) // TODO: need to detect pot changes too !
	{
		readManualMode();
	}

	// read pots
	
	potmux_update(1,1);
	potmux_update(1,1);
	potmux_update(1,1);
	potmux_update(1,1);
	
	// update CVs

	switch(frc&0x03) // 4 phases
	{
	case 0:
		// amplifier envs
		
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setCVs(&p600.ampEnvs[i],
					 currentPreset.continuousParameters[cpAmpAtt],
					 currentPreset.continuousParameters[cpAmpDec],
					 currentPreset.continuousParameters[cpAmpSus],
					 currentPreset.continuousParameters[cpAmpRel],
					 UINT16_MAX);
		break;
	case 1:
		// filter envs

		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setCVs(&p600.filEnvs[i],
					 currentPreset.continuousParameters[cpFilAtt],
					 currentPreset.continuousParameters[cpFilDec],
					 currentPreset.continuousParameters[cpFilSus],
					 currentPreset.continuousParameters[cpFilRel],
					 UINT16_MAX);
		break;
	case 2:
		// 'fixed' CVs
		
		synth_setCV(pcPModOscB,currentPreset.continuousParameters[cpPModOscB],SYNTH_FLAG_IMMEDIATE);
		synth_setCV(pcResonance,currentPreset.continuousParameters[cpResonance],SYNTH_FLAG_IMMEDIATE);
		synth_setCV(pcVolA,currentPreset.continuousParameters[cpVolA],SYNTH_FLAG_IMMEDIATE);
		synth_setCV(pcVolB,currentPreset.continuousParameters[cpVolB],SYNTH_FLAG_IMMEDIATE);
		
		// gates
		
		refreshGates();
		break;
	case 3:
		// lfo
		
		lfo_setCVs(&p600.lfo,
				currentPreset.continuousParameters[cpLFOFreq],
				satAddU16U16(currentPreset.continuousParameters[cpLFOAmt],
					potmux_getValue(ppModWheel)>>currentPreset.modwheelShift));
	
		// PW

		if(!(currentPreset.lfoTargets&(1<<modPW)))
		{
			BLOCK_INT // ensure no conflict with PWM from int
			{
				refreshPulseWidth(0);
			}
		}

		// glide
		
		p600.glideAmount=(UINT16_MAX-currentPreset.continuousParameters[cpGlide])>>5; // 11bit glide
		p600.gliding=p600.glideAmount<2000;
		break;
	}

	// volume bending
	
	synth_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),p600.benderVolumeCV),SYNTH_FLAG_IMMEDIATE);
	
	// CV computations

	computeTunedCVs();
	computeBenderCVs();
}

// 5Khz
void p600_fastInterrupt(void)
{
	uart_update();
}

// 2Khz
void p600_slowInterrupt(void)
{
	int32_t va,vf;
	int16_t pitchLfoVal,filterLfoVal,filEnvAmt,oscEnvAmt;
	int8_t v,hz63,poly,monoGlidingMask;

	static uint8_t frc=0;

	// slower updates

	hz63=(frc&0x1c)==0;	

	switch(frc&0x03) // 4 phases, each 500hz
	{
	case 0:
		if(currentPreset.lfoTargets&(1<<modPW))
		{
			refreshPulseWidth(1);
		}
		break;
	case 1:
		if(p600.gliding)
		{
			for(v=0;v<P600_VOICE_COUNT;++v)
			{
				computeGlide(&p600.oscANoteCV[v],p600.oscATargetCV[v],p600.glideAmount);
				computeGlide(&p600.oscBNoteCV[v],p600.oscBTargetCV[v],p600.glideAmount);
				computeGlide(&p600.filterNoteCV[v],p600.filterTargetCV[v],p600.glideAmount);
			}
		}
		break;
	case 2:
		if(hz63)
			handleFinishedVoices();
		
		// MIDI processing
		midi_device_process(&p600.midi);
		break;
	case 3:
		scanner_update(hz63);
		display_update(hz63);
		break;
	}
	
	++frc;

	// lfo
	
	lfo_update(&p600.lfo);
	
	pitchLfoVal=filterLfoVal=0;
	
	if(currentPreset.lfoTargets&(1<<modPitch))
		pitchLfoVal=p600.lfo.output;

	if(currentPreset.lfoTargets&(1<<modFilter))
		filterLfoVal=p600.lfo.output;
	
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
	
	poly=assigner_getMode()==mPoly;
	monoGlidingMask=((assigner_getMode()==mMonoLow || assigner_getMode()==mMonoHigh) && p600.gliding)?0x00:0xff;
	
	if(!poly)
	{
		// in any mono mode, env is always P600_MONO_ENV
		adsr_update(&p600.filEnvs[P600_MONO_ENV]);
		adsr_update(&p600.ampEnvs[P600_MONO_ENV]);
	}
	
		// P600_VOICE_COUNT calls
	updateVoice(0,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,poly);
	updateVoice(1,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,poly);
	updateVoice(2,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,poly);
	updateVoice(3,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,poly);
	updateVoice(4,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,poly);
	updateVoice(5,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,poly);
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
			int8_t shpA,shp,spd;
			
			shp=button==pbLFOShape;
			shpA=button==pb1;
			spd=button==pb2;

			readManualMode();

			if(shpA)
				currentPreset.lfoAltShapes=1-currentPreset.lfoAltShapes;
			
			if(spd)
				currentPreset.lfoShift=(currentPreset.lfoShift+1)%3;

			refreshLfoSettings(shp||shpA,spd);
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
			uint8_t type;

			type=(button==pb5)?1:0;

			currentPreset.envFlags[type]=(currentPreset.envFlags[type]+1)%4;

			refreshEnvSettings(type,1);
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

void p600_uartEvent(uint8_t data)
{
	midi_device_input(&p600.midi,1,&data);
}
