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

	uint8_t envFlags[2]; // 0:amp / 1:fil
	
	int8_t trackingShift;

	assignerMode_t assignerMonoMode;
	int8_t playingMono;
	
	int8_t lfoAltShapes;
	modulation_t lfoTargets;
	uint8_t lfoShift;
	
	int8_t modwheelShift;
	
	uint16_t benderMiddle;
	int16_t benderAmount;
	int8_t benderSemitones;
	modulation_t benderTarget;
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

static inline int16_t computeBend(p600CV_t cv)
{
	int32_t bend;
	modulation_t t;
	t=p600.benderTarget;

	bend=0;
	
	if((cv==pcMVol && t==modVolume) || (cv>=pcFil1 && cv<=pcFil6 && t==modFilter))
		bend=p600.benderSemitones*(UINT16_MAX/12);
	else if(cv>=pcOsc1A && cv<=pcOsc6B && t==modPitch)
		bend=tuner_computeCVFromNote(p600.benderSemitones*2,cv)-tuner_computeCVFromNote(0,cv);

	bend*=p600.benderAmount;
	bend/=UINT16_MAX;
	
	return bend;
}

static void adjustTunedCVs(void)
{
	int8_t v;
	uint8_t note,baseANote,baseBNote;
	int16_t mTune,fineBFreq;
	int32_t baseCutoff,amt;
	
	// compute adjusted bender amount
	
	amt=potmux_getValue(ppPitchWheel);
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

	// filters and oscs
	
	mTune=(potmux_getValue(ppMTune)>>8)+INT8_MIN;
	fineBFreq=(potmux_getValue(ppFreqBFine)>>8)+INT8_MIN;
	
	baseCutoff=potmux_getValue(ppCutoff);
	
	baseANote=potmux_getValue(ppFreqA)>>10; // 64 semitones
	baseBNote=potmux_getValue(ppFreqB)>>10;
	
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		if (!assigner_getAssignment(v,&note))
			continue;
		
		p600.oscANoteCV[v]=satAddU16S32(tuner_computeCVFromNote(baseANote+note,pcOsc1A+v),computeBend(pcOsc1A+v)+mTune);
		p600.oscBNoteCV[v]=satAddU16S32(tuner_computeCVFromNote(baseBNote+note,pcOsc1B+v),computeBend(pcOsc1B+v)+mTune+fineBFreq);
		
		if(p600.trackingShift>=0)
			p600.filterNoteCV[v]=satAddU16S32(tuner_computeCVFromNote(note,pcFil1+v)>>p600.trackingShift,computeBend(pcFil1+v)+baseCutoff);
		else
			p600.filterNoteCV[v]=satAddU16S32(tuner_computeCVFromNote(0,pcFil1+v),computeBend(pcFil1+v)+baseCutoff);
	}
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
	p600.playingMono=scanner_buttonState(pbUnison);
	
	if(p600.playingMono)
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
	
	// init
	
	scanner_init();
	display_init();
	synth_init();
	potmux_init();
	tuner_init();

	int8_t i;
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_init(&p600.ampEnvs[i]);
		adsr_init(&p600.filEnvs[i]);
	}

	// state
	
#ifndef DEBUG		
	tuner_tuneSynth();
#endif
	
	lfo_init(&p600.lfo,tuner_computeCVFromFrequency(1234,pcFil1)); // uses tuning, not random, but good enough
	
	refreshFullState();
	
	// a nice welcome message ;)
	
	sevenSeg_scrollText("GliGli's P600 upgrade "VERSION,1);
}

void p600_update(void)
{
	int8_t i;
	static uint8_t frc=0;
	int8_t updatingEnvs,updatingMisc,updatingSlow;
	
	// free running counter
	
	++frc;
	
	// toggle tape out (debug)

	BLOCK_INT
	{
		io_write(0x0e,((frc&1)<<2)|0b00110001);
	}

	// 
	
	updatingSlow=(frc&0x07)==0; // 1/8 of the time, alternatively
	updatingEnvs=(frc&0x07)==4; // 1/8 of the time, alternatively
	updatingMisc=(frc&0x03)==0; // 1/4 of the time
	
	// which pots do we have to read?
	
	if(updatingSlow)
	{
		potmux_need(ppFreqA,ppFreqB,ppMVol,ppMTune,ppLFOAmt,ppLFOFreq,ppPModFilEnv,ppPModOscB);
	}
	
	if(updatingEnvs)
	{
		potmux_need(ppAmpAtt,ppAmpDec,ppAmpSus,ppAmpRel,ppFilAtt,ppFilDec,ppFilSus,ppFilRel);
	}
	
	if(updatingMisc)
	{
		potmux_need(ppMixer,ppGlide,ppResonance,ppFilEnvAmt,ppAPW,ppBPW,ppFreqBFine,ppSpeed);
	}

	potmux_need(ppCutoff,ppPitchWheel,ppModWheel);
	
	// read them
	
	potmux_update();

	// update CVs

	if(updatingSlow)
	{
		synth_setCV(pcPModOscB,potmux_getValue(ppPModOscB),1);
		synth_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),computeBend(pcMVol)),1);
	}
	
	if(updatingEnvs)
	{
		for(i=0;i<P600_VOICE_COUNT;++i)
		{
			adsr_setCVs(&p600.ampEnvs[i],potmux_getValue(ppAmpAtt),potmux_getValue(ppAmpDec),potmux_getValue(ppAmpSus),potmux_getValue(ppAmpRel),UINT16_MAX);
			adsr_setCVs(&p600.filEnvs[i],potmux_getValue(ppFilAtt),potmux_getValue(ppFilDec),potmux_getValue(ppFilSus),potmux_getValue(ppFilRel),potmux_getValue(ppFilEnvAmt));
		}

		// when amp env finishes, voice is done

		for(i=0;i<(p600.playingMono?1:P600_VOICE_COUNT);++i)
			if (assigner_getAssignment(i,NULL) && adsr_getStage(&p600.ampEnvs[i])==sWait)
				assigner_voiceDone(i);
	}

	if(updatingMisc)
	{
		synth_setCV(pcVolA,potmux_getValue(ppMixer),1);
		synth_setCV(pcVolB,potmux_getValue(ppGlide),1);
		synth_setCV(pcResonance,potmux_getValue(ppResonance),1);
		
		if(!(p600.lfoTargets&(1<<modPW)))
		{
			synth_setCV(pcAPW,scanner_buttonState(pbASqr)?potmux_getValue(ppAPW):0,1);
			synth_setCV(pcBPW,scanner_buttonState(pbBSqr)?potmux_getValue(ppBPW):0,1);
		}
	}

	adjustTunedCVs();
	lfo_setCVs(&p600.lfo,potmux_getValue(ppLFOFreq),satAddU16U16(potmux_getValue(ppLFOAmt),potmux_getValue(ppModWheel)>>p600.modwheelShift));
}

void p600_fastInterrupt(void)
{
	int8_t v,assigned,hz500,env;
	uint16_t envVal,va,vb,vf;
	int16_t lfoVal;

	static uint8_t frc=0;
	
	hz500=(frc&0x03)==0; // 1/4 of the time (500hz)

	// lfo
	
	lfo_update(&p600.lfo);
	
	lfoVal=p600.lfo.output;
	
	if(p600.lfoTargets&(1<<modPW))
	{
		synth_setCV(pcAPW,scanner_buttonState(pbASqr)?satAddU16S16(potmux_getValue(ppAPW),lfoVal):0,1);
		synth_setCV(pcBPW,scanner_buttonState(pbBSqr)?satAddU16S16(potmux_getValue(ppBPW),lfoVal):0,1);
	}
	
	// per voice stuff
	
		// unison / mono modes use only 1 env
	
	if(p600.playingMono)
	{
		env=P600_MONO_ENV;
		adsr_update(&p600.filEnvs[env]);
		adsr_update(&p600.ampEnvs[env]);
	}
	
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		assigned=assigner_getAssignment(v,NULL);
		
		if(assigned)
		{
			// handle envs update
			
			if(!p600.playingMono)
			{
				adsr_update(&p600.filEnvs[v]);
				adsr_update(&p600.ampEnvs[v]);
				env=v;
			}

			// compute CVs

			envVal=p600.filEnvs[env].output;

			va=p600.oscANoteCV[v];
			vb=p600.oscBNoteCV[v];
			if(p600.lfoTargets&(1<<modPitch))
			{
				va=satAddU16S16(va,lfoVal);
				vb=satAddU16S16(vb,lfoVal);
			}
			
			vf=p600.filterNoteCV[v];
			vf=satAddU16U16(vf,envVal);
			if(p600.lfoTargets&(1<<modFilter))
				vf=satAddU16S16(vf,lfoVal);
			
			// apply them
			
			synth_setCV(pcOsc1A+v,va,1);
			synth_setCV(pcOsc1B+v,vb,1);
			synth_setCV(pcFil1+v,vf,1);
			synth_setCV(pcAmp1+v,p600.ampEnvs[env].output,1);
		}
	}
	
	// slower updates
	
	if(hz500)
	{
		scanner_update(); // do this first (clears display)
		display_update();
	}
	
	++frc;
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

	if((pressed && button==pb0) || button==pbUnison)
	{
		if(button==pb0)
			p600.assignerMonoMode=(p600.assignerMonoMode%mMonoHigh)+1;
		
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
	
	if(pressed && (button==pb4 || button==pb5))
	{
		char s[20]="";
		uint8_t type;
		
		type=(button==pb5)?1:0;
		
		p600.envFlags[type]=(p600.envFlags[type]+1)%4;
		
		refreshEnvSettings(type);
		
	
		if(type)
			strcat(s,"F ");
		else
			strcat(s,"A ");

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

		sevenSeg_scrollText(s,1);
	}
	
	// bender
	
	if(pressed && (button==pb7 || button==pb8))
	{
		const char * s=NULL;
		
		if (button==pb7)
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
		
		if (button==pb8)
		{
			p600.benderTarget=(p600.benderTarget+1)%(modVolume+1);
			s=modulationName(p600.benderTarget);
		}

		sevenSeg_scrollText(s,1);
	}
	
	if(pressed && button==pb9)
	{
		p600.benderMiddle=satAddU16S16(potmux_getValue(ppPitchWheel),P600_BENDER_OFFSET);

		sevenSeg_scrollText("Calibrated",1);
	}
}

void p600_keyEvent(uint8_t key, int pressed)
{
	assigner_assignNote(key,pressed);
}

void p600_assignerEvent(uint8_t note, int8_t gate, int8_t voice)
{
	if(note!=ASSIGNER_NO_NOTE)
	{
		adjustTunedCVs();
	}
	else
	{
		synth_setCV(pcAmp1+voice,0,1);
	}

	int env=p600.playingMono?P600_MONO_ENV:voice;
	
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