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

#define MOD_FILTER 1
#define MOD_FREQ 2
#define MOD_PW 4

static struct
{
	struct adsr_s filEnvs[P600_VOICE_COUNT];
	struct adsr_s ampEnvs[P600_VOICE_COUNT];

	struct lfo_s lfo;
	
	uint16_t oscANoteCVRaw[P600_VOICE_COUNT];
	uint16_t oscBNoteCVRaw[P600_VOICE_COUNT];
	uint16_t filterNoteCVRaw[P600_VOICE_COUNT]; 

	uint16_t oscANoteCVAdj[P600_VOICE_COUNT];
	uint16_t oscBNoteCVAdj[P600_VOICE_COUNT];
	uint16_t filterNoteCVAdj[P600_VOICE_COUNT]; 

	uint16_t filterEnvAmt;
	int8_t trackingShift;
	int8_t tuned;
	int8_t playingMono;

	int8_t lfoAltShapes;
	uint8_t lfoModulations;
	uint8_t lfoShift;
} p600;

static void adjustTunedCVs(void)
{
	int8_t v;
	int16_t mTune,fineBFreq;
	int32_t baseAFreq;
	int32_t baseBFreq;
	int32_t baseCutoff;
	
	mTune=(potmux_getValue(ppMTune)>>8)-128;
	fineBFreq=(potmux_getValue(ppFreqBFine)>>8)-128;
	
	baseCutoff=potmux_getValue(ppCutoff);
	
	baseAFreq=potmux_getValue(ppFreqA)>>1;
	baseAFreq+=mTune;
	
	baseBFreq=potmux_getValue(ppFreqB)>>1;
	baseBFreq+=mTune+fineBFreq;
	
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		p600.oscANoteCVAdj[v]=satAddU16S32(p600.oscANoteCVRaw[v],baseAFreq);
		p600.oscBNoteCVAdj[v]=satAddU16S32(p600.oscBNoteCVRaw[v],baseBFreq);
		p600.filterNoteCVAdj[v]=satAddU16S32(p600.filterNoteCVRaw[v]>>p600.trackingShift,baseCutoff);
	}
}

static void refreshGates(void)
{
	int8_t v;

	synth_setGate(pgASaw,scanner_buttonState(pbASaw));
	synth_setGate(pgBSaw,scanner_buttonState(pbBSaw));
	synth_setGate(pgATri,scanner_buttonState(pbATri));
	synth_setGate(pgBTri,scanner_buttonState(pbBTri));
	
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		adsr_setShape(&p600.filEnvs[v],scanner_buttonState(pbASqr));
		adsr_setShape(&p600.ampEnvs[v],scanner_buttonState(pbBSqr));
	}

	p600.trackingShift=16; // shifting any 16bit value by 16 will clear it!
	if(scanner_buttonState(pbFilFull))
		p600.trackingShift=0;
	if(scanner_buttonState(pbFilHalf))
		p600.trackingShift=1;
	
	p600.lfoModulations=0;
	if(scanner_buttonState(pbLFOFil))
		p600.lfoModulations|=MOD_FILTER;
	if(scanner_buttonState(pbLFOFreq))
		p600.lfoModulations|=MOD_FREQ;
	if(scanner_buttonState(pbLFOPW))
		p600.lfoModulations|=MOD_PW;
}

void p600_init(void)
{
	memset(&p600,0,sizeof(p600));
	
	scanner_init();
	display_init();
	synth_init();
	potmux_init();
	tuner_init();

	synth_update();
	
	int8_t i;
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_init(&p600.ampEnvs[i]);
		adsr_init(&p600.filEnvs[i]);
	}

//	tuner_tuneSynth();
	p600.tuned=1;
	
	lfo_init(&p600.lfo,tuner_computeCVFromFrequency(1234,pcFil1)); // not random, but good enough
	
	// unpressed buttons won't trigger events on start
	
	p600_buttonEvent(pbLFOShape,0);

	// a nice welcome message ;)
	
	sevenSeg_scrollText("GliGli's Prophet 600 upgrade",1);
}

void p600_update(void)
{
	int8_t i;
	static uint8_t frc=0;
	int8_t updatingEnvs,updatingMisc,updatingSlow;
	
	// tuning
	
	if(!p600.tuned)
	{
		tuner_tuneSynth();
		p600.tuned=1;
		
		// tuner will change state, restore it
		refreshGates(); 
	}
	
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
		potmux_need(ppMVol,ppMTune,ppLFOAmt,ppLFOFreq);
	}
	
	if(updatingEnvs)
	{
		potmux_need(ppAmpAtt,ppAmpDec,ppAmpSus,ppAmpRel,ppFilAtt,ppFilDec,ppFilSus,ppFilRel);
	}
	
	if(updatingMisc)
	{
		potmux_need(ppMixer,ppGlide,ppResonance,ppFilEnvAmt,ppAPW,ppBPW,ppFreqBFine);
	}

	potmux_need(ppCutoff,ppFreqA,ppFreqB);
	
	// read them
	
	potmux_update();

	// update CVs

	adjustTunedCVs();
	
	if(updatingSlow)
	{
		synth_setCV(pcMVol,potmux_getValue(ppMVol),1);
		lfo_setCVs(&p600.lfo,potmux_getValue(ppLFOFreq),potmux_getValue(ppLFOAmt));
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
		
		if(!(p600.lfoModulations&MOD_PW))
		{
			synth_setCV(pcAPW,potmux_getValue(ppAPW),1);
			synth_setCV(pcBPW,potmux_getValue(ppBPW),1);
		}
	}
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
	
	if(p600.lfoModulations&MOD_PW)
	{
		synth_setCV(pcAPW,satAddU16S16(potmux_getValue(ppAPW),lfoVal),1);
		synth_setCV(pcBPW,satAddU16S16(potmux_getValue(ppBPW),lfoVal),1);
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

			va=p600.oscANoteCVAdj[v];
			vb=p600.oscBNoteCVAdj[v];
			if(p600.lfoModulations&MOD_FREQ)
			{
				va=satAddU16S16(va,lfoVal);
				vb=satAddU16S16(vb,lfoVal);
			}
			
			vf=p600.filterNoteCVAdj[v];
			vf=satAddU16U16(vf,envVal);
			if(p600.lfoModulations&MOD_FILTER)
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
	
	if(pressed && button==pbTune)
		p600.tuned=0;

	if(pressed && button>=pb0 && button<=pb4)
	{
		assignerMode_t mode=button;

		assigner_setMode(mode);
		sevenSeg_scrollText(assigner_modeName(mode),1);
		p600.playingMono=mode!=mPoly;
	}

	if((pressed && ((button==pb5) || (button==pb6))) || button==pbLFOShape)
	{
		lfoShape_t shape;
		char s[20]="";

		if(scanner_buttonState(pb5))
			p600.lfoAltShapes=1-p600.lfoAltShapes;

		if(scanner_buttonState(pb6))
			p600.lfoShift=(p600.lfoShift+1)%3;

		shape=1+scanner_buttonState(pbLFOShape)+p600.lfoAltShapes*2;

		lfo_setShape(&p600.lfo,shape);
		lfo_setSpeedShift(&p600.lfo,p600.lfoShift*2);
		
		switch(p600.lfoShift)
		{
		case 0:
			strcat(s,"Slo ");
			break;
		case 1:
			strcat(s,"Mid ");
			break;
		case 2:
			strcat(s,"Fast ");
			break;
		}
		
		strcat(s,lfo_shapeName(shape));
		
		sevenSeg_scrollText(s,1);
	}
}

void p600_keyEvent(uint8_t key, int pressed)
{
	sevenSeg_setNumber(key);
	led_set(plFromTape,pressed,0);

	assigner_assignNote(key,pressed);
}

void p600_assignerEvent(uint8_t note, int8_t gate, int8_t voice)
{
	if(note!=ASSIGNER_NO_NOTE)
	{
		p600.oscANoteCVRaw[voice]=tuner_computeCVFromNote(note,pcOsc1A+voice);
		p600.oscBNoteCVRaw[voice]=tuner_computeCVFromNote(note,pcOsc1B+voice);
		p600.filterNoteCVRaw[voice]=tuner_computeCVFromNote(note,pcFil1+voice);
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