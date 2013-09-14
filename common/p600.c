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
#include "arp.h"
#include "storage.h"
#include "uart_6850.h"
#include "import.h"
#include "ui.h"
#include "midi.h"

#define POT_DEAD_ZONE 512

uint8_t tempBuffer[TEMP_BUFFER_SIZE]; // general purpose chunk of RAM

const p600Pot_t continuousParameterToPot[cpCount]=
{
	ppFreqA,ppMixer,ppAPW,
	ppFreqB,ppGlide,ppBPW,ppFreqBFine,
	ppCutoff,ppResonance,ppFilEnvAmt,
	ppFilRel,ppFilSus,ppFilDec,ppFilAtt,
	ppAmpRel,ppAmpSus,ppAmpDec,ppAmpAtt,
	ppPModFilEnv,ppPModOscB,
	ppLFOFreq,ppLFOAmt,
	ppNone,ppNone,ppNone,ppNone,
	ppNone,ppNone,ppNone,ppNone,
};

volatile uint32_t currentTick=0; // 500hz

struct p600_s
{
	struct adsr_s filEnvs[P600_VOICE_COUNT];
	struct adsr_s ampEnvs[P600_VOICE_COUNT];

	struct lfo_s lfo,vibrato;
	
	uint16_t oscANoteCV[P600_VOICE_COUNT];
	uint16_t oscBNoteCV[P600_VOICE_COUNT];
	uint16_t filterNoteCV[P600_VOICE_COUNT]; 
	
	uint16_t oscATargetCV[P600_VOICE_COUNT];
	uint16_t oscBTargetCV[P600_VOICE_COUNT];
	uint16_t filterTargetCV[P600_VOICE_COUNT];

	int16_t benderAmount;
	int16_t benderCVs[pcFil6-pcOsc1A+1];
	int16_t benderVolumeCV;

	int16_t glideAmount;
	int8_t gliding;
	
	uint32_t modulationDelayStart;
	uint16_t modulationDelayTickCount;
} p600;

extern void refreshAllPresetButtons(void);


static void computeTunedCVs(int8_t force, int8_t forceVoice)
{
	uint16_t cva,cvb,cvf;
	uint8_t note,baseCutoffNote,baseANote,baseBNote,trackingNote;
	int8_t v;

	uint16_t baseAPitch,baseBPitch,baseCutoff;
	int16_t mTune,fineBFreq,detune;

	static uint16_t baseAPitchRaw,baseBPitchRaw,baseCutoffRaw,mTuneRaw,fineBFreqRaw,detuneRaw;
	static uint8_t track,chrom;
	
	// detect change & quit if none
	
	if(!force && 
		mTuneRaw==potmux_getValue(ppMTune) &&
		fineBFreqRaw==currentPreset.continuousParameters[cpFreqBFine] &&
		baseCutoffRaw==currentPreset.continuousParameters[cpCutoff] &&
		baseAPitchRaw==currentPreset.continuousParameters[cpFreqA] &&
		baseBPitchRaw==currentPreset.continuousParameters[cpFreqB] &&
		detuneRaw==currentPreset.continuousParameters[cpUnisonDetune] &&
		track==currentPreset.steppedParameters[spTrackingShift] &&
		chrom==currentPreset.steppedParameters[spChromaticPitch])
	{
		return;
	}
	
	mTuneRaw=potmux_getValue(ppMTune);
	fineBFreqRaw=currentPreset.continuousParameters[cpFreqBFine];
	baseCutoffRaw=currentPreset.continuousParameters[cpCutoff];
	baseAPitchRaw=currentPreset.continuousParameters[cpFreqA];
	baseBPitchRaw=currentPreset.continuousParameters[cpFreqB];
	detuneRaw=currentPreset.continuousParameters[cpUnisonDetune];
	track=currentPreset.steppedParameters[spTrackingShift];
	chrom=currentPreset.steppedParameters[spChromaticPitch];
	
	// compute for oscs & filters
	
	mTune=(mTuneRaw>>7)+INT8_MIN*2;
	fineBFreq=(fineBFreqRaw>>7)+INT8_MIN*2;
	baseCutoff=((uint32_t)baseCutoffRaw*5)>>3; // 62.5% of raw cutoff
	baseAPitch=baseAPitchRaw>>2;
	baseBPitch=baseBPitchRaw>>2;
	
	baseCutoffNote=baseCutoff>>8;
	baseANote=baseAPitch>>8; // 64 semitones
	baseBNote=baseBPitch>>8;
	
	baseCutoff&=0xff;
	
	if(chrom>0)
	{
		baseAPitch=0;
		baseBPitch=0;
		
		if(chrom>1)
		{
			baseANote-=baseANote%12;
			baseBNote-=baseBNote%12;
		}
	}
	else
	{
		baseAPitch&=0xff;
		baseBPitch&=0xff;
	}

	for(v=0;v<P600_VOICE_COUNT;++v)
	{
		if ((forceVoice>=0 && v!=forceVoice) || !assigner_getAssignment(v,&note))
			continue;

		// oscs
		
		cva=satAddU16S32(tuner_computeCVFromNote(baseANote+note,baseAPitch,pcOsc1A+v),(int32_t)p600.benderCVs[pcOsc1A+v]+mTune);
		cvb=satAddU16S32(tuner_computeCVFromNote(baseBNote+note,baseBPitch,pcOsc1B+v),(int32_t)p600.benderCVs[pcOsc1B+v]+mTune+fineBFreq);
		
		if(currentPreset.steppedParameters[spUnison])
		{
			detune=(1+(v>>1))*(v&1?-1:1)*(detuneRaw>>8);

			cva=satAddU16S16(cva,detune);
			cvb=satAddU16S16(cvb,detune);
		}
		
		// filter
		
		trackingNote=baseCutoffNote;
		if(track)
			trackingNote+=note>>(2-track);
			
		cvf=satAddU16S16(tuner_computeCVFromNote(trackingNote,baseCutoff,pcFil1+v),p600.benderCVs[pcFil1+v]);
		
		// glide
		
		if(p600.gliding)
		{
			p600.oscATargetCV[v]=cva;
			p600.oscBTargetCV[v]=cvb;
			p600.filterTargetCV[v]=cvf;

			if(!track)
				p600.filterNoteCV[v]=cvf; // no glide if no tracking for filter
		}
		else			
		{
			p600.oscANoteCV[v]=cva;
			p600.oscBNoteCV[v]=cvb;
			p600.filterNoteCV[v]=cvf;
		}
				
	}
}

void computeBenderCVs(void)
{
	int32_t bend,amt;
	uint16_t pos;
	p600CV_t cv;

	pos=potmux_getValue(ppPitchWheel);
	
	// compute adjusted bender amount
	
	amt=pos;
	
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
		amt/=UINT16_MAX-settings.benderMiddle;
	}
	p600.benderAmount=MIN(MAX(amt,INT16_MIN),INT16_MAX);
	
	// compute bends
	
	switch(currentPreset.steppedParameters[spBenderTarget])
	{
	case modVCO:
		for(cv=pcOsc1A;cv<=pcOsc6B;++cv)
		{
			bend=tuner_computeCVFromNote(currentPreset.steppedParameters[spBenderSemitones]*2,0,cv)-tuner_computeCVFromNote(0,0,cv);
			bend*=p600.benderAmount;
			bend/=UINT16_MAX;
			p600.benderCVs[cv]=bend;
		}
		break;
	case modVCF:
		bend=currentPreset.steppedParameters[spBenderSemitones];
		bend*=p600.benderAmount;
		bend/=12;
		for(cv=pcFil1;cv<=pcFil6;++cv)
			p600.benderCVs[cv]=bend;
		break;
	case modVCA:
		bend=currentPreset.steppedParameters[spBenderSemitones];
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

static void refreshModulationDelay(int8_t refreshTickCount)
{
	int8_t anyPressed;
	
	anyPressed=assigner_getAnyPressed();	
	
	if(!anyPressed)
	{
		p600.modulationDelayStart=UINT32_MAX;
	}
	else if (p600.modulationDelayStart==UINT32_MAX)
	{
		p600.modulationDelayStart=currentTick;
	}
	
	if(refreshTickCount)
		p600.modulationDelayTickCount=exponentialCourse(UINT16_MAX-currentPreset.continuousParameters[cpModDelay],12000.0f,2500.0f);
}

static void handleFinishedVoices(void)
{
	int8_t v;
	
	// when amp env finishes, voice is done
	
	for(v=0;v<P600_VOICE_COUNT;++v)
		if(assigner_getAssignment(v,NULL) && adsr_getStage(&p600.ampEnvs[v])==sWait)
			assigner_voiceDone(v);
}

static void refreshGates(void)
{
	synth_setGate(pgASaw,currentPreset.steppedParameters[spASaw]);
	synth_setGate(pgBSaw,currentPreset.steppedParameters[spBSaw]);
	synth_setGate(pgATri,currentPreset.steppedParameters[spATri]);
	synth_setGate(pgBTri,currentPreset.steppedParameters[spBTri]);
	synth_setGate(pgSync,currentPreset.steppedParameters[spSync]);
	synth_setGate(pgPModFA,currentPreset.steppedParameters[spPModFA]);
	synth_setGate(pgPModFil,currentPreset.steppedParameters[spPModFil]);
}

static inline void refreshPulseWidth(int8_t pwm)
{
	int32_t pa,pb;
	
	pa=pb=UINT16_MAX; // in various cases, defaulting this CV to zero made PW still bleed into audio (eg osc A with sync)

	uint8_t sqrA=currentPreset.steppedParameters[spASqr];
	uint8_t sqrB=currentPreset.steppedParameters[spBSqr];

	if(sqrA)
		pa=currentPreset.continuousParameters[cpAPW];

	if(sqrB)
		pb=currentPreset.continuousParameters[cpBPW];

	if(pwm)
	{
		if(sqrA && !(currentPreset.steppedParameters[spLFOTargets]&mtOnlyB))
			pa+=p600.lfo.output;

		if(sqrB && !(currentPreset.steppedParameters[spLFOTargets]&mtOnlyA))
			pb+=p600.lfo.output;
	}

	BLOCK_INT
	{
		synth_setCV32Sat_FastPath(pcAPW,pa);
		synth_setCV32Sat_FastPath(pcBPW,pb);
	}
}

static void refreshAssignerSettings(void)
{
	assigner_setPattern(currentPreset.voicePattern,currentPreset.steppedParameters[spUnison]);
	assigner_setVoiceMask(settings.voiceMask);
	assigner_setPriority(currentPreset.steppedParameters[spAssignerPriority]);
}

static void refreshEnvSettings(int8_t type)
{
	uint8_t expo,slow;
	int8_t i;
	struct adsr_s * a;
		
	expo=currentPreset.steppedParameters[(type)?spFilEnvExpo:spAmpEnvExpo];
	slow=currentPreset.steppedParameters[(type)?spFilEnvSlow:spAmpEnvSlow];

	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		if(type)
			a=&p600.filEnvs[i];
		else
			a=&p600.ampEnvs[i];

		adsr_setShape(a,expo);
		adsr_setSpeedShift(a,(slow)?3:1);
	}
}

static void refreshLfoSettings(void)
{
	lfoShape_t shape;
	uint8_t shift;

	shape=currentPreset.steppedParameters[spLFOShape];
	shift=1+currentPreset.steppedParameters[spLFOShift]*3;

	lfo_setShape(&p600.lfo,shape);
	lfo_setSpeedShift(&p600.lfo,shift);
}

static void refreshSevenSeg(void)
{
	if(ui.digitInput<diLoadDecadeDigit)
	{
		uint8_t v=ui.manualActivePotValue;
		sevenSeg_setNumber(v);
		led_set(plDot,v>99,v>199);
	}
	else
	{
		if(ui.digitInput!=diLoadDecadeDigit)
		{
			if(ui.presetAwaitingNumber>=0)
				sevenSeg_setAscii('0'+ui.presetAwaitingNumber,' ');
			else
				sevenSeg_setAscii(' ',' ');
		}
		else
		{
			sevenSeg_setNumber(settings.presetNumber);
			led_set(plDot,ui.presetModified,0);
		}
	}

	led_set(plPreset,settings.presetMode,0);
	led_set(plToTape,ui.digitInput==diSynth && settings.presetMode,0);
	led_set(plFromTape,scanner_buttonState(pbFromTape),0);
	
	if(arp_getMode()!=amOff)
	{
		led_set(plRecord,arp_getHold(),0);
		led_set(plArpUD,arp_getMode()==amUpDown,0);
		led_set(plArpAssign,arp_getMode()!=amUpDown,arp_getMode()==amRandom);
	}
	else
	{
		led_set(plRecord,ui.digitInput==diStoreDecadeDigit,ui.digitInput==diStoreDecadeDigit);
		led_set(plArpUD,0,0);
		led_set(plArpAssign,0,0);
	}
	
}

void refreshFullState(void)
{
	refreshModulationDelay(1);
	refreshGates();
	refreshAssignerSettings();
	refreshLfoSettings();
	refreshEnvSettings(0);
	refreshEnvSettings(1);
	computeBenderCVs();
	
	refreshSevenSeg();
}

static void refreshPresetPots(int8_t force)
{
	continuousParameter_t cp;
	
	for(cp=0;cp<cpCount;++cp)
		if((continuousParameterToPot[cp]!=ppNone) && (force || continuousParameterToPot[cp]==ui.lastActivePot || potmux_hasChanged(continuousParameterToPot[cp])))
		{
			currentPreset.continuousParameters[cp]=potmux_getValue(continuousParameterToPot[cp]);
			ui.presetModified=1;
		}
}

void refreshPresetMode(void)
{
	if(!preset_loadCurrent(settings.presetMode?settings.presetNumber:MANUAL_PRESET_PAGE))
		preset_loadDefault(1);

	if(!settings.presetMode)
		refreshAllPresetButtons();
	
	ui.lastActivePot=ppNone;
	ui.presetModified=0;
	ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth;
}

static FORCEINLINE void refreshVoice(int8_t v,int16_t oscEnvAmt,int16_t filEnvAmt,int16_t pitchALfoVal,int16_t pitchBLfoVal,int16_t filterLfoVal)
{
	int32_t va,vb,vf;
	uint16_t envVal;
	int8_t assigned;
	
	assigned=assigner_getAssignment(v,NULL);
	
	if(assigned)
	{
		// update envs, compute CVs & apply them

		adsr_update(&p600.filEnvs[v]);
		envVal=p600.filEnvs[v].output;

		va=pitchALfoVal;
		vb=pitchBLfoVal;

		// osc B

		vb+=p600.oscBNoteCV[v];
		synth_setCV32Sat_FastPath(pcOsc1B+v,vb);

		// osc A

		va+=scaleU16S16(envVal,oscEnvAmt);	
		va+=p600.oscANoteCV[v];
		synth_setCV32Sat_FastPath(pcOsc1A+v,va);

		// filter

		vf=filterLfoVal;
		vf+=scaleU16S16(envVal,filEnvAmt);
		vf+=p600.filterNoteCV[v];
		synth_setCV32Sat_FastPath(pcFil1+v,vf);

		// amplifier

		adsr_update(&p600.ampEnvs[v]);
		synth_setCV_FastPath(pcAmp1+v,p600.ampEnvs[v].output);
	}
	else
	{
		CYCLE_WAIT(40); // 10us (helps for snappiness, because it lets some time for previous voice CVs to stabilize)
		synth_setCV_FastPath(pcAmp1+v,0);
	}
}

////////////////////////////////////////////////////////////////////////////////
// P600 main code
////////////////////////////////////////////////////////////////////////////////

void p600_init(void)
{
	int8_t i;
	
	// init
	
	memset(&p600,0,sizeof(p600));
	
	scanner_init();
	display_init();
	synth_init();
	potmux_init();
	tuner_init();
	assigner_init();
	uart_init();
	arp_init();
	ui_init();
	midi_init();
	
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_init(&p600.ampEnvs[i]);
		adsr_init(&p600.filEnvs[i]);
	}

	lfo_init(&p600.lfo);
	lfo_init(&p600.vibrato);
	lfo_setShape(&p600.vibrato,lsTri);
	lfo_setSpeedShift(&p600.vibrato,4);
	
	// initial input state
	
	scanner_update(1);
	potmux_update(POTMUX_POT_COUNT);

	// manual preset
	
	if(!preset_loadCurrent(MANUAL_PRESET_PAGE))
	{
		preset_loadDefault(0);
		preset_saveCurrent(MANUAL_PRESET_PAGE);
	}
	
	// load settings from storage; tune when they are bad
	
	if(!settings_load())
	{
		settings_loadDefault();
		
#ifndef DEBUG
		tuner_tuneSynth();
#endif	
	}

	// load last preset & do a full refresh
	
	refreshPresetMode();
	refreshFullState();
	
	// a nice welcome message, and we're ready to go :)
	
	sevenSeg_scrollText("GliGli's P600 upgrade "VERSION,1);
}

void p600_update(void)
{
	int8_t i,wheelChange,wheelUpdate,dlyMod;
	uint8_t potVal;
	uint16_t mwAmt,lfoAmt,vibAmt;
	static uint8_t frc=0;
	static uint32_t bendChangeStart=0;
	
	// toggle tape out (debug)

	BLOCK_INT
	{
		++frc;
		io_write(0x0e,((frc&1)<<2)|0b00110001);
	}

	// update pots, detecting change

	potmux_resetChanged();
	potmux_update(4);
	
	// act on pot change
	
	if(potmux_lastChanged()!=ppNone)
		ui_dataPotChanged();

	refreshPresetPots(!settings.presetMode);

	// has to stay outside of previous if, so that finer pot values changes can also be displayed
	
	potVal=potmux_getValue(ui.lastActivePot)>>8;
	if(potVal!=ui.manualActivePotValue)
	{
		ui.manualActivePotValue=potVal;
		refreshSevenSeg();
	}

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
					 0,0x0f);

		// filter envs

		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setCVs(&p600.filEnvs[i],
					 currentPreset.continuousParameters[cpFilAtt],
					 currentPreset.continuousParameters[cpFilDec],
					 currentPreset.continuousParameters[cpFilSus],
					 currentPreset.continuousParameters[cpFilRel],
					 0,0x0f);

		// modulations
		
		dlyMod=currentTick-p600.modulationDelayStart>p600.modulationDelayTickCount;
		mwAmt=potmux_getValue(ppModWheel)>>currentPreset.steppedParameters[spModwheelShift];

		lfoAmt=currentPreset.continuousParameters[cpLFOAmt];
		lfoAmt=(lfoAmt<POT_DEAD_ZONE)?0:(lfoAmt-POT_DEAD_ZONE);
			
		vibAmt=currentPreset.continuousParameters[cpVibAmt]>>2;
		vibAmt=(vibAmt<POT_DEAD_ZONE)?0:(vibAmt-POT_DEAD_ZONE);
		
		if(currentPreset.steppedParameters[spModwheelTarget]==0) // targeting lfo?
		{
			lfo_setCVs(&p600.lfo,
					currentPreset.continuousParameters[cpLFOFreq],
					satAddU16U16(lfoAmt,mwAmt));
			lfo_setCVs(&p600.vibrato,
					 currentPreset.continuousParameters[cpVibFreq],
					 dlyMod?vibAmt:0);
		}
		else
		{
			lfo_setCVs(&p600.lfo,
					currentPreset.continuousParameters[cpLFOFreq],
					dlyMod?lfoAmt:0);
			lfo_setCVs(&p600.vibrato,
					currentPreset.continuousParameters[cpVibFreq],
					satAddU16U16(vibAmt,mwAmt));
		}
		
		
		break;
	case 1:
		// 'fixed' CVs
		
		synth_setCV(pcPModOscB,currentPreset.continuousParameters[cpPModOscB],SYNTH_FLAG_IMMEDIATE);
		synth_setCV(pcResonance,currentPreset.continuousParameters[cpResonance],SYNTH_FLAG_IMMEDIATE);
		break;
	case 2:
		// 'fixed' CVs
		
		synth_setCV(pcVolA,currentPreset.continuousParameters[cpVolA],SYNTH_FLAG_IMMEDIATE);
		synth_setCV(pcVolB,currentPreset.continuousParameters[cpVolB],SYNTH_FLAG_IMMEDIATE);
		synth_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),p600.benderVolumeCV),SYNTH_FLAG_IMMEDIATE);
		break;
	case 3:
		// gates
		
		refreshGates();

		// glide
		
		p600.glideAmount=exponentialCourse(currentPreset.continuousParameters[cpGlide],11000.0f,2100.0f);
		p600.gliding=p600.glideAmount<2000;
		
		// arp
		
		arp_setSpeed(currentPreset.continuousParameters[cpSeqArpClock]);
		
		break;
	}

	// CV computations

		// bender
	
	wheelChange=potmux_hasChanged(ppPitchWheel);
	wheelUpdate=wheelChange || bendChangeStart+TICKER_1S>currentTick;
	
	if(wheelUpdate)
	{
		computeBenderCVs();

		// volume bending
		synth_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),p600.benderVolumeCV),SYNTH_FLAG_IMMEDIATE);
		
		if(wheelChange)
			bendChangeStart=currentTick;
	}

		// tuned CVs

	computeTunedCVs(wheelUpdate,-1);
}

////////////////////////////////////////////////////////////////////////////////
// P600 interrupts
////////////////////////////////////////////////////////////////////////////////

void p600_uartInterrupt(void)
{
	uart_update();
}

// 2Khz
void p600_timerInterrupt(void)
{
	int32_t va,vf;
	int16_t pitchALfoVal,pitchBLfoVal,filterLfoVal,filEnvAmt,oscEnvAmt;
	int8_t v,hz63;

	static uint8_t frc=0;

	// lfo
	
	lfo_update(&p600.lfo);
	
	pitchALfoVal=pitchBLfoVal=p600.vibrato.output;
	filterLfoVal=0;
	
	if(currentPreset.steppedParameters[spLFOTargets]&mtVCO)
	{
		if(!(currentPreset.steppedParameters[spLFOTargets]&mtOnlyB))
			pitchALfoVal+=p600.lfo.output>>1;
		if(!(currentPreset.steppedParameters[spLFOTargets]&mtOnlyA))
			pitchBLfoVal+=p600.lfo.output>>1;
	}

	if(currentPreset.steppedParameters[spLFOTargets]&mtVCF)
		filterLfoVal=p600.lfo.output;
	
	// global env computations
	
	vf=currentPreset.continuousParameters[cpFilEnvAmt];
	vf+=INT16_MIN;
	filEnvAmt=vf;
	
	oscEnvAmt=0;
	if(currentPreset.steppedParameters[spPModFA])
	{
		va=currentPreset.continuousParameters[cpPModFilEnv];
		va+=INT16_MIN;
		va/=2; // half strength
		oscEnvAmt=va;		
	}
	
	// per voice stuff
	
		// P600_VOICE_COUNT calls
	refreshVoice(0,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(1,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(2,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(3,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(4,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(5,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	
	// slower updates
	
	BLOCK_INT
	{
		hz63=(frc&0x1c)==0;	

		switch(frc&0x03) // 4 phases, each 500hz
		{
		case 0:
			if(hz63)
				handleFinishedVoices();

			// MIDI processing
			midi_update();

			// ticker inc
			++currentTick;
			break;
		case 1:
			if(arp_getMode()!=amOff)
			{
				arp_update();
			}

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
			lfo_update(&p600.vibrato);
			refreshPulseWidth(currentPreset.steppedParameters[spLFOTargets]&mtPW);
			break;
		case 3:
			scanner_update(hz63);
			display_update(hz63);
			break;
		}

		++frc;
	}
}

////////////////////////////////////////////////////////////////////////////////
// P600 internal events
////////////////////////////////////////////////////////////////////////////////

void LOWERCODESIZE p600_buttonEvent(p600Button_t button, int pressed)
{
	ui_handleButton(button,pressed);
}

void p600_keyEvent(uint8_t key, int pressed)
{
	if(arp_getMode()==amOff)
	{
		assigner_assignNote(key,pressed,UINT16_MAX,0);
	}
	else
	{
		arp_assignNote(key,pressed);
	}
}

void p600_assignerEvent(uint8_t note, int8_t gate, int8_t voice, uint16_t velocity, int8_t legato)
{
	uint16_t velAmt;
	
	// mod delay

	refreshModulationDelay(0);

	// prepare CVs

	computeTunedCVs(1,voice);

	// set gates (don't retrigger gate, unless we're arpeggiating)

	if(!legato || arp_getMode()!=amOff)
	{
		adsr_setGate(&p600.filEnvs[voice],gate);
		adsr_setGate(&p600.ampEnvs[voice],gate);
	}

	if(gate)
	{
		// handle velocity

		velAmt=currentPreset.continuousParameters[cpFilVelocity];
		adsr_setCVs(&p600.filEnvs[voice],0,0,0,0,(UINT16_MAX-velAmt)+scaleU16U16(velocity,velAmt),0x10);
		velAmt=currentPreset.continuousParameters[cpAmpVelocity];
		adsr_setCVs(&p600.ampEnvs[voice],0,0,0,0,(UINT16_MAX-velAmt)+scaleU16U16(velocity,velAmt),0x10);
	}

#ifdef DEBUG
	print("assign note ");
	phex(note);
	print("  gate ");
	phex(gate);
	print(" voice ");
	phex(voice);
	print(" velocity ");
	phex16(velocity);
	print("\n");
#endif
}

void p600_uartEvent(uint8_t data)
{
	midi_newData(data);
}
