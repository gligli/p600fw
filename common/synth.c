////////////////////////////////////////////////////////////////////////////////
// Top level code
////////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "synth.h"

#include "scanner.h"
#include "display.h"
#include "sh.h"
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
#include "../xnormidi/midi.h"

#define POT_DEAD_ZONE 512

// Dead band is distance from center of pot to end of dead band area,
// in either direction.
#define BEND_DEADBAND 3072
// Guard band is distance from the end of pot travel until we start
// reacting. Compensates for the fact that the bend pot cannot reach
// especially the maximum positive voltage.
#define BEND_GUARDBAND 400

#define PANEL_DEADBAND 2048

#define BIT_INTPUT_FOOTSWITCH 0x20
#define BIT_INTPUT_TAPE_IN 0x01

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

struct synth_s
{
	struct adsr_s filEnvs[SYNTH_VOICE_COUNT];
	struct adsr_s ampEnvs[SYNTH_VOICE_COUNT];

	struct lfo_s lfo,vibrato;
	
	uint16_t oscANoteCV[SYNTH_VOICE_COUNT];
	uint16_t oscBNoteCV[SYNTH_VOICE_COUNT];
	uint16_t filterNoteCV[SYNTH_VOICE_COUNT]; 
	
	uint16_t oscATargetCV[SYNTH_VOICE_COUNT];
	uint16_t oscBTargetCV[SYNTH_VOICE_COUNT];
	uint16_t filterTargetCV[SYNTH_VOICE_COUNT];

	uint16_t modwheelAmount;
	int16_t benderAmount;
	int16_t benderCVs[pcFil6-pcOsc1A+1];
	int16_t benderVolumeCV;

	int16_t glideAmount;
	int8_t gliding;
	
	uint32_t modulationDelayStart;
	uint16_t modulationDelayTickCount;
	
	uint8_t pendingExtClock;
	
	int8_t transpose;
} synth;

extern void refreshAllPresetButtons(void);
extern const uint16_t attackCurveLookup[]; // for modulation delay

struct deadband {
	uint16_t middle;
	uint16_t guard;
	uint16_t deadband;
	uint32_t precalcLow;
	uint32_t precalcHigh;
};

struct deadband bendDeadband = { HALF_RANGE, BEND_GUARDBAND,  BEND_DEADBAND };
struct deadband panelDeadband = { HALF_RANGE, 0, PANEL_DEADBAND };

static void computeTunedCVs(int8_t force, int8_t forceVoice)
{
	uint16_t cva,cvb,cvf;
	uint8_t note,baseCutoffNote;
	int8_t v;

	uint16_t baseAPitch,baseBPitch,baseCutoff;
	int16_t mTune,fineBFreq,detune;

	// We use int16_t here because we want to be able to use negative
	// values for intermediate calculations, while still retaining a
	// maximum value of at least UINT8_T.
	int16_t sNote,baseANote,ANote,baseBNote,BNote,trackingNote;

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

	for(v=0;v<SYNTH_VOICE_COUNT;++v)
	{
		if ((forceVoice>=0 && v!=forceVoice) || !assigner_getAssignment(v,&note))
			continue;

		// Subtract bottom C, signed result. Here a value of 0
		// is lowest C on kbd, values below that can arrive via MIDI
		sNote=note-SCANNER_BASE_NOTE;

		// oscs

		ANote=baseANote+sNote;
		if (ANote<0)
			ANote=0;
		// We assume we won't get more than UINT8_MAX here, even
		// if the incoming MIDI note is high and baseANote is large too.
		BNote=baseBNote+sNote;
		if (BNote<0)
			BNote=0;
		
		cva=satAddU16S32(tuner_computeCVFromNote(ANote,baseAPitch,pcOsc1A+v),(int32_t)synth.benderCVs[pcOsc1A+v]+mTune);
		cvb=satAddU16S32(tuner_computeCVFromNote(BNote,baseBPitch,pcOsc1B+v),(int32_t)synth.benderCVs[pcOsc1B+v]+mTune+fineBFreq);
		
		// filter
		
		trackingNote=baseCutoffNote;
		if(track) {
			// We use / instead of >> because sNote is signed. */
			// Using a constant instead of calculated value
			// for the divisor as it gives the compiler a chance to
			// optimize using shift operations.
			// >> is not guaranteed in C to work properly for
			// signed numbers (implementation-specific). */
			trackingNote+=(track==1?sNote/2:sNote);
			// can only be negative if tracking is enabled. */
			if (trackingNote<0)
				trackingNote=0;
		}
			
		cvf=satAddU16S16(tuner_computeCVFromNote(trackingNote,baseCutoff,pcFil1+v),synth.benderCVs[pcFil1+v]);

		// detune
		
		if(currentPreset.steppedParameters[spUnison] || settings.spread)
		{
			detune=(1+(v>>1))*(v&1?-1:1)*(detuneRaw>>8);

			cva=satAddU16S16(cva,detune);
			cvb=satAddU16S16(cvb,detune);
			cvf=satAddU16S16(cvf,detune);
		}
		
		// glide
		
		if(synth.gliding)
		{
			synth.oscATargetCV[v]=cva;
			synth.oscBTargetCV[v]=cvb;
			synth.filterTargetCV[v]=cvf;

			if(!track)
				synth.filterNoteCV[v]=cvf; // no glide if no tracking for filter
		}
		else			
		{
			synth.oscANoteCV[v]=cva;
			synth.oscBNoteCV[v]=cvb;
			synth.filterNoteCV[v]=cvf;
		}
				
	}
}

// Precalculate factor for dead band scaling to avoid time consuming
// division operation.
// so instead of doing foo*=32768; foo/=factor; we precalculate
// precalc=32768<<16/factor, and do foo*=precalc; foo>>=16; runtime.
static void precalcDeadband(struct deadband *d)
{
	uint16_t middleLow=d->middle-d->deadband;
	uint16_t middleHigh=d->middle+d->deadband;

	d->precalcLow=HALF_RANGE_L/(middleLow-d->guard);
	d->precalcHigh=HALF_RANGE_L/(FULL_RANGE-d->guard-middleHigh);
}

static inline uint16_t addDeadband(uint16_t value, struct deadband *d)
{
	uint16_t middleLow=d->middle-d->deadband;
	uint16_t middleHigh=d->middle+d->deadband;
	uint32_t amt;

	if(value>FULL_RANGE-d->guard)
		return FULL_RANGE;
	if(value<d->guard)
		return 0;

	amt=value;

	if(value<middleLow) {
		amt-=d->guard;
		amt*=d->precalcLow; // result is 65536 too big now
	} else if(value>middleHigh) {
		amt-=middleHigh;
		amt*=d->precalcHigh; // result is 65536 too big now
		amt+=HALF_RANGE_L;
	} else { // in deadband
		return HALF_RANGE;
	}
	// result of our calculations will be 0..UINT16_MAX<<16
	return amt>>16;
}

static inline int16_t getAdjustedBenderAmount(void)
{
	return addDeadband(potmux_getValue(ppPitchWheel),&bendDeadband)-HALF_RANGE;
}

void synth_updateBender(void)
{
	bendDeadband.middle=settings.benderMiddle;
	precalcDeadband(&bendDeadband);
	synth_wheelEvent(getAdjustedBenderAmount(),0,1,0);
}

void computeBenderCVs(void)
{
	int32_t bend;
	p600CV_t cv;

	// compute bends
	
		// reset old bends
		
	for(cv=pcOsc1A;cv<=pcFil6;++cv)
		synth.benderCVs[cv]=0;
	synth.benderVolumeCV=0;
	
		// compute new

	switch(currentPreset.steppedParameters[spBenderTarget])
	{
	case modVCO:
		for(cv=pcOsc1A;cv<=pcOsc6B;++cv)
		{
			bend=tuner_computeCVFromNote(currentPreset.steppedParameters[spBenderSemitones]*2,0,cv)-tuner_computeCVFromNote(0,0,cv);
			bend*=synth.benderAmount;
			bend/=UINT16_MAX;
			synth.benderCVs[cv]=bend;
		}
		break;
	case modVCF:
		bend=currentPreset.steppedParameters[spBenderSemitones];
		bend*=synth.benderAmount;
		bend/=12;
		for(cv=pcFil1;cv<=pcFil6;++cv)
			synth.benderCVs[cv]=bend;
		break;
	case modVCA:
		bend=currentPreset.steppedParameters[spBenderSemitones];
		bend*=synth.benderAmount;
		bend/=12;
		synth.benderVolumeCV=bend;
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
	int8_t anyPressed, anyAssigned;
	static int8_t prevAnyPressed=0;
	
	anyPressed=assigner_getAnyPressed();	
	anyAssigned=assigner_getAnyAssigned();	
	
	if(!anyAssigned)
	{
		synth.modulationDelayStart=UINT32_MAX;
	}
	
	if(anyPressed && !prevAnyPressed)
	{
		synth.modulationDelayStart=currentTick;
	}
	
	prevAnyPressed=anyPressed;
	
	if(refreshTickCount)
		synth.modulationDelayTickCount=exponentialCourse(UINT16_MAX-currentPreset.continuousParameters[cpModDelay],12000.0f,2500.0f);
}

static void handleFinishedVoices(void)
{
	int8_t v;
	
	for(v=0;v<SYNTH_VOICE_COUNT;++v)
	{
		// when amp env finishes, voice is done
		if(assigner_getAssignment(v,NULL) && adsr_getStage(&synth.ampEnvs[v])==sWait)
			assigner_voiceDone(v);
	
		// if voice isn't assigned, silence it
		if(!assigner_getAssignment(v,NULL) && adsr_getStage(&synth.ampEnvs[v])!=sWait)
		{
			adsr_reset(&synth.ampEnvs[v]);
			adsr_reset(&synth.filEnvs[v]);
		}
	}
}

static void refreshGates(void)
{
	sh_setGate(pgASaw,currentPreset.steppedParameters[spASaw]);
	sh_setGate(pgBSaw,currentPreset.steppedParameters[spBSaw]);
	sh_setGate(pgATri,currentPreset.steppedParameters[spATri]);
	sh_setGate(pgBTri,currentPreset.steppedParameters[spBTri]);
	sh_setGate(pgSync,currentPreset.steppedParameters[spSync]);
	sh_setGate(pgPModFA,currentPreset.steppedParameters[spPModFA]);
	sh_setGate(pgPModFil,currentPreset.steppedParameters[spPModFil]);
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
			pa+=synth.lfo.output;

		if(sqrB && !(currentPreset.steppedParameters[spLFOTargets]&mtOnlyA))
			pb+=synth.lfo.output;
	}

	BLOCK_INT
	{
		sh_setCV32Sat_FastPath(pcAPW,pa);
		sh_setCV32Sat_FastPath(pcBPW,pb);
	}
}

static void refreshAssignerSettings(void)
{
	if(currentPreset.steppedParameters[spUnison])
		assigner_setPattern(currentPreset.voicePattern,1);
	else
		assigner_setPoly();
		
	assigner_setVoiceMask(settings.voiceMask);
	assigner_setPriority(currentPreset.steppedParameters[spAssignerPriority]);
}

static void refreshEnvSettings(void)
{
	int8_t i;
	uint16_t aa,ad,as,ar,fa,fd,fs,fr;
	int16_t spread;
	
	as=currentPreset.continuousParameters[cpAmpSus];
	fs=currentPreset.continuousParameters[cpFilSus];
	
	for(i=0;i<SYNTH_VOICE_COUNT;++i)
	{
		adsr_setShape(&synth.ampEnvs[i],currentPreset.steppedParameters[spAmpEnvExpo]);
		adsr_setShape(&synth.filEnvs[i],currentPreset.steppedParameters[spFilEnvExpo]);
		
		adsr_setSpeedShift(&synth.ampEnvs[i],(currentPreset.steppedParameters[spAmpEnvSlow])?3:1);
		adsr_setSpeedShift(&synth.filEnvs[i],(currentPreset.steppedParameters[spFilEnvSlow])?3:1);
		
		spread=0;
		if(settings.spread)
			spread=((1+(i>>1))*(i&1?-1:1))<<8;
		
		aa=satAddU16S16(currentPreset.continuousParameters[cpAmpAtt],spread);
		ad=satAddU16S16(currentPreset.continuousParameters[cpAmpDec],spread);
		ar=satAddU16S16(currentPreset.continuousParameters[cpAmpRel],spread);

		fa=satAddU16S16(currentPreset.continuousParameters[cpFilAtt],spread);
		fd=satAddU16S16(currentPreset.continuousParameters[cpFilDec],spread);
		fr=satAddU16S16(currentPreset.continuousParameters[cpFilRel],spread);

		adsr_setCVs(&synth.ampEnvs[i],aa,ad,as,ar,0,0x0f);
		adsr_setCVs(&synth.filEnvs[i],fa,fd,fs,fr,0,0x0f);
	}
}

static void refreshLfoSettings(void)
{
	lfoShape_t shape;
	uint8_t shift;
	uint16_t mwAmt,lfoAmt,vibAmt,dlyAmt;
	uint32_t elapsed;

	shape=currentPreset.steppedParameters[spLFOShape];
	shift=1+currentPreset.steppedParameters[spLFOShift]*3;

	lfo_setShape(&synth.lfo,shape);
	lfo_setSpeedShift(&synth.lfo,shift);
	
	// wait modulationDelayTickCount then progressively increase over
	// modulationDelayTickCount time, following an exponential curve
	dlyAmt=0;
	if(synth.modulationDelayStart!=UINT32_MAX)
	{
		if(currentPreset.continuousParameters[cpModDelay]<POT_DEAD_ZONE)
		{
			dlyAmt=UINT16_MAX;
		}
		else if(currentTick>=synth.modulationDelayStart+synth.modulationDelayTickCount)
		{
			elapsed=currentTick-(synth.modulationDelayStart+synth.modulationDelayTickCount);
			if(elapsed>=synth.modulationDelayTickCount)
				dlyAmt=UINT16_MAX;
			else
				dlyAmt=attackCurveLookup[(elapsed<<8)/synth.modulationDelayTickCount];
		}
	}
	
	mwAmt=synth.modwheelAmount>>currentPreset.steppedParameters[spModwheelShift];

	lfoAmt=currentPreset.continuousParameters[cpLFOAmt];
	lfoAmt=(lfoAmt<POT_DEAD_ZONE)?0:(lfoAmt-POT_DEAD_ZONE);

	vibAmt=currentPreset.continuousParameters[cpVibAmt]>>2;
	vibAmt=(vibAmt<POT_DEAD_ZONE)?0:(vibAmt-POT_DEAD_ZONE);

	if(currentPreset.steppedParameters[spModwheelTarget]==0) // targeting lfo?
	{
		lfo_setCVs(&synth.lfo,
				currentPreset.continuousParameters[cpLFOFreq],
				satAddU16U16(lfoAmt,mwAmt));
		lfo_setCVs(&synth.vibrato,
				 currentPreset.continuousParameters[cpVibFreq],
				 scaleU16U16(vibAmt,dlyAmt));
	}
	else
	{
		lfo_setCVs(&synth.lfo,
				currentPreset.continuousParameters[cpLFOFreq],
				scaleU16U16(lfoAmt,dlyAmt));
		lfo_setCVs(&synth.vibrato,
				currentPreset.continuousParameters[cpVibFreq],
				satAddU16U16(vibAmt,mwAmt));
	}
}

static void refreshSevenSeg(void)
{
	if(ui.digitInput<diLoadDecadeDigit)
	{
		led_set(plDot,0,0);
		
		if(ui.lastActivePotValue>=0)
		{
			int32_t v;
			
			if(ui.lastActivePot!=ppPitchWheel)
			{
				v=ui.adjustedLastActivePotValue;
			}
			else
			{
				v=getAdjustedBenderAmount();
				v-=INT16_MIN;
			}

			v=(v*100L)>>16; // 0..100 range
		
			if(potmux_isPotZeroCentered(ui.lastActivePot))
			{
				v=abs(v-50);
				led_set(plDot,ui.adjustedLastActivePotValue<=INT16_MAX,0); // dot indicates negative
			}
			
			sevenSeg_setNumber(v);
		}
		else
		{
			sevenSeg_setAscii(' ',' ');
		}
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
	refreshEnvSettings();
	computeBenderCVs();
	
	refreshSevenSeg();
}

static void refreshPresetPots(int8_t force)
{
	continuousParameter_t cp;
	
	for(cp=0;cp<cpCount;++cp)
		if((continuousParameterToPot[cp]!=ppNone) && (force || continuousParameterToPot[cp]==ui.lastActivePot || potmux_hasChanged(continuousParameterToPot[cp])))
		{
			p600Pot_t pp=continuousParameterToPot[cp];
			uint16_t value=potmux_getValue(pp);

			if(potmux_isPotZeroCentered(pp))
				value=addDeadband(value,&panelDeadband);
			currentPreset.continuousParameters[cp]=value;
			ui.presetModified=1;
		}
}

void refreshPresetMode(void)
{
	if(!preset_loadCurrent(settings.presetMode?settings.presetNumber:MANUAL_PRESET_PAGE))
		preset_loadDefault(1);

	if(!settings.presetMode)
	{
		refreshAllPresetButtons();
		refreshPresetPots(1);
	}
	
	ui_setNoActivePot();
	ui.presetModified=0;
	ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth;
}

static FORCEINLINE void refreshVoice(int8_t v,int16_t oscEnvAmt,int16_t filEnvAmt,int16_t pitchALfoVal,int16_t pitchBLfoVal,int16_t filterLfoVal)
{
	int32_t va,vb,vf;
	uint16_t envVal;
	
	BLOCK_INT
	{
		// update envs, compute CVs & apply them

		adsr_update(&synth.filEnvs[v]);
		envVal=synth.filEnvs[v].output;

		va=pitchALfoVal;
		vb=pitchBLfoVal;

		// osc B

		vb+=synth.oscBNoteCV[v];
		sh_setCV32Sat_FastPath(pcOsc1B+v,vb);

		// osc A

		va+=scaleU16S16(envVal,oscEnvAmt);	
		va+=synth.oscANoteCV[v];
		sh_setCV32Sat_FastPath(pcOsc1A+v,va);

		// filter

		vf=filterLfoVal;
		vf+=scaleU16S16(envVal,filEnvAmt);
		vf+=synth.filterNoteCV[v];
		sh_setCV32Sat_FastPath(pcFil1+v,vf);

		// amplifier

		adsr_update(&synth.ampEnvs[v]);
		sh_setCV_FastPath(pcAmp1+v,synth.ampEnvs[v].output);
	}
}

static void handleBitInputs(void)
{
	uint8_t cur;
	static uint8_t last=0;
	
	BLOCK_INT
	{
		cur=io_read(0x9);
	}
	
	// control footswitch 
	 
	if(currentPreset.steppedParameters[spUnison] && !(cur&BIT_INTPUT_FOOTSWITCH) && last&BIT_INTPUT_FOOTSWITCH)
	{
		assigner_latchPattern();
		assigner_getPattern(currentPreset.voicePattern,NULL);
	}
	else if((cur&BIT_INTPUT_FOOTSWITCH)!=(last&BIT_INTPUT_FOOTSWITCH))
	{
		if(arp_getMode()!=amOff)
			arp_setMode(arp_getMode(),(cur&BIT_INTPUT_FOOTSWITCH)?0:2);
		else
		{
			assigner_holdEvent((cur&BIT_INTPUT_FOOTSWITCH)?0:1);
			midi_sendSustainEvent((cur&BIT_INTPUT_FOOTSWITCH)?0:1);
		}
	}

	// tape in
	
	if(settings.syncMode==smTape && cur&BIT_INTPUT_TAPE_IN && !(last&BIT_INTPUT_TAPE_IN))
	{
		++synth.pendingExtClock;
	}
	
	// this must stay last
	
	last=cur;
}

////////////////////////////////////////////////////////////////////////////////
// P600 main code
////////////////////////////////////////////////////////////////////////////////

void synth_init(void)
{
	int8_t i;
	
	// init
	
	memset(&synth,0,sizeof(synth));
	
	scanner_init();
	display_init();
	sh_init();
	potmux_init();
	tuner_init();
	assigner_init();
	uart_init();
	arp_init();
	ui_init();
	midi_init();
	
	for(i=0;i<SYNTH_VOICE_COUNT;++i)
	{
		adsr_init(&synth.ampEnvs[i]);
		adsr_init(&synth.filEnvs[i]);
	}

	lfo_init(&synth.lfo);
	lfo_init(&synth.vibrato);
	lfo_setShape(&synth.vibrato,lsTri);
	lfo_setSpeedShift(&synth.vibrato,4);
	
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

	// initial input state
	
	scanner_update(1);
	potmux_update(POTMUX_POT_COUNT);

	// dead band pre calculation
	precalcDeadband(&panelDeadband);
	bendDeadband.middle=settings.benderMiddle;
	precalcDeadband(&bendDeadband);

	// load last preset & do a full refresh
	
	refreshPresetMode();
	refreshFullState();
	
	// a nice welcome message, and we're ready to go :)
	
	sevenSeg_scrollText("GliGli's P600 upgrade "VERSION,1);
}

void synth_update(void)
{
	int32_t potVal;
	static uint8_t frc=0;
	
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
	
	ui_checkIfDataPotChanged();

	refreshPresetPots(!settings.presetMode);

	if(ui.lastActivePot!=ppNone)
	{
		potVal=potmux_getValue(ui.lastActivePot);
		if(potVal!=ui.lastActivePotValue)
		{
			ui.lastActivePotValue=potVal;
			ui.adjustedLastActivePotValue=potVal;

			if(potmux_isPotZeroCentered(ui.lastActivePot))
				ui.adjustedLastActivePotValue=addDeadband(potVal,&panelDeadband);

			refreshSevenSeg();

			// update CVs

			if(ui.lastActivePot==ppModWheel)
				synth_wheelEvent(0,potmux_getValue(ppModWheel),2,1);
			else if(ui.lastActivePot==ppPitchWheel)
				synth_wheelEvent(getAdjustedBenderAmount(),0,1,1);
			else if (ui.lastActivePot==ppAmpAtt || ui.lastActivePot==ppAmpDec ||
					ui.lastActivePot==ppAmpSus || ui.lastActivePot==ppAmpRel ||
					ui.lastActivePot==ppFilAtt || ui.lastActivePot==ppFilDec ||
					ui.lastActivePot==ppFilSus || ui.lastActivePot==ppFilRel)
				refreshEnvSettings();
		}
	}
	
	switch(frc&0x03) // 4 phases
	{
	case 0:
		// lfo (for mod delay)

		refreshLfoSettings();
		break;
	case 1:
		// 'fixed' CVs
		
		sh_setCV(pcPModOscB,currentPreset.continuousParameters[cpPModOscB],SH_FLAG_IMMEDIATE);
		sh_setCV(pcResonance,currentPreset.continuousParameters[cpResonance],SH_FLAG_IMMEDIATE);
		sh_setCV(pcExtFil,24576,SH_FLAG_IMMEDIATE); // value from the emulator
		break;
	case 2:
		// 'fixed' CVs
		
		sh_setCV(pcVolA,currentPreset.continuousParameters[cpVolA],SH_FLAG_IMMEDIATE);
		sh_setCV(pcVolB,currentPreset.continuousParameters[cpVolB],SH_FLAG_IMMEDIATE);
		sh_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),synth.benderVolumeCV),SH_FLAG_IMMEDIATE);
		break;
	case 3:
		// gates
		
		refreshGates();

		// glide
		
		synth.glideAmount=exponentialCourse(currentPreset.continuousParameters[cpGlide],11000.0f,2100.0f);
		synth.gliding=synth.glideAmount<2000;
		
		// arp
		
		arp_setSpeed(currentPreset.continuousParameters[cpSeqArpClock]);
		
		break;
	}

	// tuned CVs

	computeTunedCVs(0,-1);
}

////////////////////////////////////////////////////////////////////////////////
// P600 interrupts
////////////////////////////////////////////////////////////////////////////////

void synth_uartInterrupt(void)
{
	uart_update();
}

// 2Khz
void synth_timerInterrupt(void)
{
	int32_t va,vf;
	int16_t pitchALfoVal,pitchBLfoVal,filterLfoVal,filEnvAmt,oscEnvAmt;
	int8_t v,hz63,hz250;

	static uint8_t frc=0;

	// lfo
	
	lfo_update(&synth.lfo);
	
	pitchALfoVal=pitchBLfoVal=synth.vibrato.output;
	filterLfoVal=0;
	
	if(currentPreset.steppedParameters[spLFOTargets]&mtVCO)
	{
		if(!(currentPreset.steppedParameters[spLFOTargets]&mtOnlyB))
			pitchALfoVal+=synth.lfo.output>>1;
		if(!(currentPreset.steppedParameters[spLFOTargets]&mtOnlyA))
			pitchBLfoVal+=synth.lfo.output>>1;
	}

	if(currentPreset.steppedParameters[spLFOTargets]&mtVCF)
		filterLfoVal=synth.lfo.output;
	
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
	
		// SYNTH_VOICE_COUNT calls
	refreshVoice(0,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(1,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(2,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(3,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(4,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	refreshVoice(5,oscEnvAmt,filEnvAmt,pitchALfoVal,pitchBLfoVal,filterLfoVal);
	
	// slower updates
	
	hz63=(frc&0x1c)==0;	
	hz250=(frc&0x04)==0;	

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
		// bit inputs (footswitch / tape in)
	
		handleBitInputs();
		
		// arpeggiator

		if(arp_getMode()!=amOff && (settings.syncMode==smInternal || synth.pendingExtClock))
		{
			if(synth.pendingExtClock)
				--synth.pendingExtClock;
			
			arp_update();
		}

		// glide
		
		if(synth.gliding)
		{
			for(v=0;v<SYNTH_VOICE_COUNT;++v)
			{
				computeGlide(&synth.oscANoteCV[v],synth.oscATargetCV[v],synth.glideAmount);
				computeGlide(&synth.oscBNoteCV[v],synth.oscBTargetCV[v],synth.glideAmount);
				computeGlide(&synth.filterNoteCV[v],synth.filterTargetCV[v],synth.glideAmount);
			}
		}

		break;
	case 2:
		lfo_update(&synth.vibrato);
		refreshPulseWidth(currentPreset.steppedParameters[spLFOTargets]&mtPW);
		break;
	case 3:
		if(hz250)
		{
			scanner_update(hz63);
			display_update(hz63);
		}
		break;
	}

	++frc;
}

////////////////////////////////////////////////////////////////////////////////
// P600 internal events
////////////////////////////////////////////////////////////////////////////////

void LOWERCODESIZE synth_buttonEvent(p600Button_t button, int pressed)
{
	ui_handleButton(button,pressed);
}

void synth_keyEvent(uint8_t key, int pressed)
{
	if(ui.isTransposing)
	{
		if(pressed)
		{
			char s[16]="transp = ";
			
			synth.transpose=(int8_t)key-SCANNER_C2;
			arp_setTranspose(synth.transpose);
			
			itoa(synth.transpose,&s[9],10);
			sevenSeg_scrollText(s,1);
		}
	}
	else if(arp_getMode()==amOff)
	{
		// Set velocity to half (corresponding to MIDI value 64)
		assigner_assignNote(key+synth.transpose,pressed,HALF_RANGE);

		// pass to MIDI out
		midi_sendNoteEvent(key+synth.transpose,pressed,HALF_RANGE);
	}
	else
	{
		arp_assignNote(key,pressed);
	}
}

void synth_assignerEvent(uint8_t note, int8_t gate, int8_t voice, uint16_t velocity, int8_t legato)
{
	uint16_t velAmt;
	
	// mod delay

	refreshModulationDelay(0);

	// prepare CVs

	computeTunedCVs(1,voice);

	// set gates (don't retrigger gate, unless we're arpeggiating)

	if(!legato || arp_getMode()!=amOff)
	{
		adsr_setGate(&synth.filEnvs[voice],gate);
		adsr_setGate(&synth.ampEnvs[voice],gate);
	}

	if(gate)
	{
		// handle velocity

		velAmt=currentPreset.continuousParameters[cpFilVelocity];
		adsr_setCVs(&synth.filEnvs[voice],0,0,0,0,(UINT16_MAX-velAmt)+scaleU16U16(velocity,velAmt),0x10);
		velAmt=currentPreset.continuousParameters[cpAmpVelocity];
		adsr_setCVs(&synth.ampEnvs[voice],0,0,0,0,(UINT16_MAX-velAmt)+scaleU16U16(velocity,velAmt),0x10);
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

void synth_uartEvent(uint8_t data)
{
	midi_newData(data);
}

void synth_wheelEvent(int16_t bend, uint16_t modulation, uint8_t mask, int8_t outputToMidi)
{
	if(mask&1)
	{
		synth.benderAmount=bend;
		computeBenderCVs();
		computeTunedCVs(1,-1);
	}
	
	if(mask&2)
	{
		synth.modwheelAmount=modulation;
		refreshLfoSettings();
	}
	
	// pass to MIDI out
	
	if(outputToMidi)
		midi_sendWheelEvent(bend,modulation,mask);
}

void synth_realtimeEvent(uint8_t midiEvent)
{
	if(settings.syncMode!=smMIDI)
		return;
	
	switch(midiEvent)
	{
		case MIDI_CLOCK:
			++synth.pendingExtClock;
			break;
		case MIDI_START:
			arp_resetCounter();
			synth.pendingExtClock=0;
			break;
	}
}
