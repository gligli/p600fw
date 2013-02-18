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
#include "tuner.h"
#include "assigner.h"

#define P600_UNISON_ENV 0

static struct
{
	struct adsr_s filEnvs[P600_VOICE_COUNT];
	struct adsr_s ampEnvs[P600_VOICE_COUNT];

	uint16_t oscANoteCVRaw[P600_VOICE_COUNT];
	uint16_t oscBNoteCVRaw[P600_VOICE_COUNT];
	uint16_t filterNoteCVRaw[P600_VOICE_COUNT]; 

	uint16_t oscANoteCVAdj[P600_VOICE_COUNT];
	uint16_t oscBNoteCVAdj[P600_VOICE_COUNT];
	uint16_t filterNoteCVAdj[P600_VOICE_COUNT]; 

	uint16_t filterEnvAmt;
	int8_t trackingShift;
	int8_t tuned;
	int8_t playingUnison;
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
		p600.oscANoteCVAdj[v]=SADD16(p600.oscANoteCVRaw[v],baseAFreq);
		p600.oscBNoteCVAdj[v]=SADD16(p600.oscBNoteCVRaw[v],baseBFreq);
		p600.filterNoteCVAdj[v]=SADD16(p600.filterNoteCVRaw[v]>>p600.trackingShift,baseCutoff);
	}
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

	tuner_tuneSynth();
	p600.tuned=1;
	
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
	}
	
	// free running counter
	
	++frc;
	
	// toggle tape out (debug)

	BLOCK_INT
	{
		io_write(0x0e,((frc&1)<<2)|0b00110001);
	}

	// 
	
	updatingSlow=(frc&0x0f)==0; // 1/16 of the time
	updatingEnvs=(frc&0x07)==0; // 1/8 of the time
	updatingMisc=(frc&0x03)==0; // 1/4 of the time
	
	// which pots do we have to read?
	
	if(updatingSlow)
	{
		potmux_need(ppMVol,ppMTune);
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
	}
	
	if(updatingEnvs)
	{
		for(i=0;i<P600_VOICE_COUNT;++i)
		{
			adsr_setCVs(&p600.ampEnvs[i],potmux_getValue(ppAmpAtt),potmux_getValue(ppAmpDec),potmux_getValue(ppAmpSus),potmux_getValue(ppAmpRel),UINT16_MAX);
			adsr_setCVs(&p600.filEnvs[i],potmux_getValue(ppFilAtt),potmux_getValue(ppFilDec),potmux_getValue(ppFilSus),potmux_getValue(ppFilRel),potmux_getValue(ppFilEnvAmt));
		}

		// when amp env finishes, voice is done

		for(i=0;i<(p600.playingUnison?1:P600_VOICE_COUNT);++i)
			if (assigner_getAssignment(i,NULL) && adsr_getStage(&p600.ampEnvs[i])==sWait)
				assigner_voiceDone(i);
	}

	if(updatingMisc)
	{
		synth_setCV(pcVolA,potmux_getValue(ppMixer),1);
		synth_setCV(pcVolB,potmux_getValue(ppGlide),1);
		synth_setCV(pcAPW,potmux_getValue(ppAPW),1);
		synth_setCV(pcBPW,potmux_getValue(ppBPW),1);
		synth_setCV(pcRes,potmux_getValue(ppResonance),1);
	}
}

void p600_fastInterrupt(void)
{
	int8_t v,assigned,hz250,env;
	uint16_t envVal,filVal;

	static uint8_t frc=0;
	
	hz250=(frc&0x07)==0; // 1/8 of the time (250hz)

	if(p600.playingUnison)
	{
		adsr_update(&p600.filEnvs[P600_UNISON_ENV]);
		adsr_update(&p600.ampEnvs[P600_UNISON_ENV]);
		env=P600_UNISON_ENV;
	}
	
	// per voice stuff
	
	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		assigned=assigner_getAssignment(v,NULL);
		
		if(assigned)
		{
			// handle envs update
			
			if(!p600.playingUnison)
			{
				adsr_update(&p600.filEnvs[v]);
				adsr_update(&p600.ampEnvs[v]);
				env=v;
			}

			// handle CVs update

			synth_setCV(pcOsc1A+v,p600.oscANoteCVAdj[v],1);
			synth_setCV(pcOsc1B+v,p600.oscBNoteCVAdj[v],1);

			envVal=p600.filEnvs[env].output;
			filVal=p600.filterNoteCVAdj[v];
			synth_setCV(pcFil1+v,SADD16(envVal,filVal),1);
			synth_setCV(pcAmp1+v,p600.ampEnvs[env].output,1);
		}
	}
	
	// slower updates
	
	if(hz250)
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
	int8_t i;

	p600.trackingShift=32; // shifting any value by 32 will clear it!
	
	switch(button)
	{
	case pbASaw:
		synth_setGate(pgASaw,pressed);
		break;
	case pbBSaw:
		synth_setGate(pgBSaw,pressed);
		break;
	case pbATri:
		synth_setGate(pgATri,pressed);
		break;
	case pbBTri:
		synth_setGate(pgBTri,pressed);
		break;
	case pbASqr:
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setShape(&p600.filEnvs[i],pressed);
		break;
	case pbBSqr:
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setShape(&p600.ampEnvs[i],pressed);
		break;
	case pbTune:
		if (!pressed)
			p600.tuned=0;
		break;
	case pbFilHalf:
		if(pressed)
			p600.trackingShift=1;
		break;
	case pbFilFull:
		if(pressed)
			p600.trackingShift=0;
		else
		break;
	default:
		;
	}
	
	if(pressed && button>=pb0 && button<=pb4)
	{
		assignerMode_t mode=button;
		
		assigner_setMode(mode);
		sevenSeg_scrollText(assigner_modeName(mode),1);
		p600.playingUnison=mode==mUnison;
	}
}

void p600_keyEvent(uint8_t key, int pressed)
{
	sevenSeg_setNumber(key);
	led_set(plFromTape,pressed,0);

	if(pressed)
	{
		assigner_assignNote(key);
	}
	else
	{
		int8_t v;

		v=assigner_getVoiceFromNote(key);
		
		if(v>=-1)
		{
			v=MAX(P600_UNISON_ENV,v); // unison is env 0

			adsr_setGate(&p600.filEnvs[v],0);
			adsr_setGate(&p600.ampEnvs[v],0);
		}
	}
}

void p600_assignerEvent(uint8_t note, int8_t voice)
{
	if(note!=ASSIGNER_NO_NOTE)
	{
		if(!p600.playingUnison || voice==P600_UNISON_ENV)
		{
			adsr_setGate(&p600.filEnvs[voice],1);
			adsr_setGate(&p600.ampEnvs[voice],1);
		}

		p600.oscANoteCVRaw[voice]=tuner_computeCVFromNote(note,pcOsc1A+voice);
		p600.oscBNoteCVRaw[voice]=tuner_computeCVFromNote(note,pcOsc1B+voice);
		p600.filterNoteCVRaw[voice]=tuner_computeCVFromNote(note,pcFil1+voice);
		adjustTunedCVs();
	}
	else
	{
		synth_setCV(pcAmp1+voice,0,1); // silence remaining notes
	}

#ifdef DEBUG		
	print("assign ");
	phex(note);
	print(" ");
	phex(voice);
	print("\n");
#endif
}