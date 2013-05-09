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
#include "arp.h"
#include "storage.h"
#include "uart_6850.h"

#define P600_MONO_ENV 0 // informative constant, don't change it!

#define MIDI_BASE_STEPPED_CC 32
#define MIDI_BASE_COARSE_CC 64
#define MIDI_BASE_FINE_CC 96
#define MIDI_BASE_NOTE 12

#define TICKER_1S 500

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

static struct
{
	volatile uint32_t ticker; // 500hz
	
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

	int16_t benderAmount;
	int16_t benderCVs[pcFil6-pcOsc1A+1];
	int16_t benderVolumeCV;

	int16_t glideAmount;
	int8_t gliding;
	
	p600Pot_t manualDisplayedPot;
	
	enum {pdiNone,pdiLoadDecadeDigit,pdiStoreDecadeDigit,pdiLoadUnitDigit,pdiStoreUnitDigit} presetDigitInput;
	int8_t presetAwaitingNumber;
	int8_t presetModified;
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

static void computeTunedCVs(int8_t force)
{
	uint16_t cva,cvb,cvf,uVal;
	uint8_t note,baseCutoffNote,baseANote,baseBNote,trackingNote;
	int8_t v,vm,monoMask;

	uint16_t baseAPitch,baseBPitch,baseCutoff;
	int16_t mTune,fineBFreq;

	static uint16_t baseAPitchRaw,baseBPitchRaw,baseCutoffRaw,mTuneRaw,fineBFreqRaw;
	static uint8_t track,chrom;
	
	// detect change & quit if none
	
	uVal=potmux_getValue(ppMTune);
	if(mTuneRaw!=uVal)
	{
		mTuneRaw=uVal;
		force=1;
	}
	
	uVal=currentPreset.continuousParameters[cpFreqBFine];
	if(fineBFreqRaw!=uVal)
	{
		fineBFreqRaw=uVal;
		force=1;
	}
	
	uVal=currentPreset.continuousParameters[cpCutoff];
	if(baseCutoffRaw!=uVal)
	{
		baseCutoffRaw=uVal;
		force=1;
	}
	
	uVal=currentPreset.continuousParameters[cpFreqA];
	if(baseAPitchRaw!=uVal)
	{
		baseAPitchRaw=uVal;
		force=1;
	}

	uVal=currentPreset.continuousParameters[cpFreqB];
	if(baseBPitchRaw!=uVal)
	{
		baseBPitchRaw=uVal;
		force=1;
	}
	
	if(track!=currentPreset.steppedParameters[spTrackingShift])
	{
		track=currentPreset.steppedParameters[spTrackingShift];
		force=1;
	}
	
	if(chrom!=currentPreset.steppedParameters[spChromaticPitch])
	{
		chrom=currentPreset.steppedParameters[spChromaticPitch];
		force=1;
	}
	
	if(!force)
		return;

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
	
	if(chrom)
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
		if(track)
			trackingNote+=note>>(2-track);
			
		cvf=satAddU16S16(tuner_computeCVFromNote(trackingNote,baseCutoff,pcFil1+v),p600.benderCVs[pcFil1+v]);
		
		// glide
		
		if(p600.gliding)
		{
			monoMask=(assigner_getMode()==mMonoLow || assigner_getMode()==mMonoHigh)?0x00:0xff;
			vm=v&monoMask;			

			p600.oscATargetCV[vm]=cva;
			p600.oscBTargetCV[vm]=cvb;
			p600.filterTargetCV[vm]=cvf;

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

static void computeBenderCVs(void)
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
	case modPitch:
		for(cv=pcOsc1A;cv<=pcOsc6B;++cv)
		{
			bend=tuner_computeCVFromNote(currentPreset.steppedParameters[spBenderSemitones]*2,0,cv)-tuner_computeCVFromNote(0,0,cv);
			bend*=p600.benderAmount;
			bend/=UINT16_MAX;
			p600.benderCVs[cv]=bend;
		}
		break;
	case modFilter:
		bend=currentPreset.steppedParameters[spBenderSemitones];
		bend*=p600.benderAmount;
		bend/=12;
		for(cv=pcFil1;cv<=pcFil6;++cv)
			p600.benderCVs[cv]=bend;
		break;
	case modVolume:
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

static void handleFinishedVoices(void)
{
	int8_t v,poly;
	
	poly=assigner_getMode()==mPoly;

	// when amp env finishes, voice is done
	
	for(v=0;v<P600_VOICE_COUNT;++v)
		if(assigner_getAssignment(v,NULL) && adsr_getStage(&p600.ampEnvs[poly?v:P600_MONO_ENV])==sWait)
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
		if(sqrA)
			pa+=p600.lfo.output;

		if(sqrB)
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
	if(currentPreset.steppedParameters[spUnison])
		assigner_setMode(currentPreset.steppedParameters[spAssignerMonoMode]);
	else
		assigner_setMode(mPoly);
}

static void refreshEnvSettings(int8_t type, int8_t display)
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
	
	if(display)
	{
		char s[20];
		
		strcpy(s,(slow)?"slo":"fast");
		strcat(s,(expo)?" exp":" lin");
		strcat(s,(type)?" fil":" amp");

		sevenSeg_scrollText(s,1);
	}
}

static void refreshLfoSettings(int8_t dispShape,int8_t dispSpd)
{
	const char * s[3]={"Slo","Med","Fast"};
	
	lfoShape_t shape;
	uint8_t shift;

	shape=currentPreset.steppedParameters[spLFOShape];
	shift=currentPreset.steppedParameters[spLFOShift];

	// set random seed for random-based shapes
	if(shape==lsRand || shape==lsNoise)
		srand(p600.ticker);
	
	lfo_setShape(&p600.lfo,shape);
	lfo_setSpeedShift(&p600.lfo,shift*2);

	if(dispShape)
	{
		sevenSeg_scrollText(lfo_shapeName(shape),1);
	}
	else if(dispSpd && shift<=2)
	{
		sevenSeg_scrollText(s[shift],1);
	}
}

static void refreshSevenSeg(void)
{
	if(p600.presetDigitInput==pdiNone)
	{
		if(p600.manualDisplayedPot!=ppNone)
		{
			uint8_t v=potmux_getValue(p600.manualDisplayedPot)>>8; // show 8 bits
			sevenSeg_setNumber(v);
			led_set(plDot,v>99,v>199);
		}
	}
	else
	{
		if(p600.presetDigitInput!=pdiLoadDecadeDigit)
		{
			if(p600.presetAwaitingNumber>=0)
				sevenSeg_setAscii('0'+p600.presetAwaitingNumber,' ');
			else
				sevenSeg_setAscii(' ',' ');
		}
		else
		{
			sevenSeg_setNumber(settings.presetNumber);
			led_set(plDot,p600.presetModified,0);
		}
	}

	led_set(plPreset,settings.presetBank!=pbkManual,settings.presetBank==pbkB);
	
	if(arp_getMode()!=amOff)
	{
		led_set(plRecord,arp_getHold(),0);
		led_set(plArpUD,arp_getMode()==amUpDown,0);
		led_set(plArpAssign,arp_getMode()!=amUpDown,arp_getMode()==amRandom);
	}
	else
	{
		led_set(plRecord,p600.presetDigitInput==pdiStoreDecadeDigit,p600.presetDigitInput==pdiStoreDecadeDigit);
		led_set(plArpUD,0,0);
		led_set(plArpAssign,0,0);
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

static void refreshPresetPots(int8_t force)
{
	continuousParameter_t cp;
	
	for(cp=0;cp<cpCount;++cp)
		if(force || potmux_hasChanged(continuousParameterToPot[cp]))
		{
			currentPreset.continuousParameters[cp]=potmux_getValue(continuousParameterToPot[cp]);
			p600.presetModified=1;
		}
}

static void refreshPresetButton(p600Button_t button)
{
	uint8_t bitState;
	int8_t change=1;
	
	bitState=scanner_buttonState(button)?1:0;
	
	switch(button)
	{
	case pbASaw:
		currentPreset.steppedParameters[spASaw]=bitState;
		break;
	case pbATri:
		currentPreset.steppedParameters[spATri]=bitState;
		break;
	case pbASqr:
		currentPreset.steppedParameters[spASqr]=bitState;
		break;
	case pbBSaw:
		currentPreset.steppedParameters[spBSaw]=bitState;
		break;
	case pbBTri:
		currentPreset.steppedParameters[spBTri]=bitState;
		break;
	case pbBSqr:
		currentPreset.steppedParameters[spBSqr]=bitState;
		break;
	case pbSync:
		currentPreset.steppedParameters[spSync]=bitState;
		break;
	case pbPModFA:
		currentPreset.steppedParameters[spPModFA]=bitState;
		break;
	case pbPModFil:
		currentPreset.steppedParameters[spPModFil]=bitState;
		break;
	case pbUnison:
		currentPreset.steppedParameters[spUnison]=bitState;
		break;
	case pbLFOShape:
		currentPreset.steppedParameters[spLFOShape]&=~1;
		currentPreset.steppedParameters[spLFOShape]|=scanner_buttonState(pbLFOShape)?1:0;
		break;
	case pbLFOFreq:
	case pbLFOPW:
	case pbLFOFil:
		currentPreset.steppedParameters[spLFOTargets]=
			(scanner_buttonState(pbLFOFreq)?mtPitch:0) +
			(scanner_buttonState(pbLFOPW)?mtPW:0) +
			(scanner_buttonState(pbLFOFil)?mtFilter:0);
		break;
	case pbFilFull:
	case pbFilHalf:
		currentPreset.steppedParameters[spTrackingShift]=
			(scanner_buttonState(pbFilHalf)?1:0) +
			(scanner_buttonState(pbFilFull)?2:0);
		break;
	default:
		change=0;
	}
	
	if(change)
	{
		p600.presetModified=1;
		refreshFullState();
	}
}


static FORCEINLINE void refreshVoice(int8_t v,int16_t oscEnvAmt,int16_t filEnvAmt,int16_t pitchLfoVal,int16_t filterLfoVal,int8_t monoGlidingMask,int8_t polyMask)
{
	int32_t va,vb,vf;
	uint16_t envVal;
	int8_t assigned,envVoice,pitchVoice;
	
	envVoice=v&polyMask;

	assigned=assigner_getAssignment(v,NULL);
	
	BLOCK_INT
	{
		if(assigned)
		{
			if(polyMask)
			{
				// handle envs update
				adsr_update(&p600.filEnvs[v]);
				adsr_update(&p600.ampEnvs[v]);
			}

			pitchVoice=v&monoGlidingMask;

			// compute CVs & apply them

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
		else
		{
			synth_setCV_FastPath(pcAmp1+v,0);
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
	
	assigner_assignNote(intNote,velocity!=0,((velocity+1)<<9)-1,0);
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
	
	assigner_assignNote(intNote,0,0,0);
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
	
	if(settings.presetBank==pbkManual) // in manual mode CC changes would only conflict with pot scans...
		return;
	
	if(control>=MIDI_BASE_COARSE_CC && control<MIDI_BASE_COARSE_CC+cpCount)
	{
		param=control-MIDI_BASE_COARSE_CC;

		currentPreset.continuousParameters[param]&=0x01fc;
		currentPreset.continuousParameters[param]|=(uint16_t)value<<9;
		p600.presetModified=1;	
	}
	else if(control>=MIDI_BASE_FINE_CC && control<MIDI_BASE_FINE_CC+cpCount)
	{
		param=control-MIDI_BASE_FINE_CC;

		currentPreset.continuousParameters[param]&=0xfe00;
		currentPreset.continuousParameters[param]|=(uint16_t)value<<2;
		p600.presetModified=1;	
	}
	else if(control>=MIDI_BASE_STEPPED_CC && control<MIDI_BASE_STEPPED_CC+spCount)
	{
		param=control-MIDI_BASE_STEPPED_CC;
		
		currentPreset.steppedParameters[param]=value>>(7-steppedParametersBits[param]);
		phex(param);phex(currentPreset.steppedParameters[param]);print("\n");
		p600.presetModified=1;	
	}

	if(p600.presetModified)
		refreshFullState();
}

void p600_init(void)
{
	memset(&p600,0,sizeof(p600));
	memset(&settings,0,sizeof(settings));
	memset(&currentPreset,0,sizeof(currentPreset));
	
	// defaults
	
	p600.presetDigitInput=pdiNone;
	p600.presetAwaitingNumber=-1;
	p600.manualDisplayedPot=ppNone;
	settings.benderMiddle=UINT16_MAX/2;
	settings.presetBank=pbkManual;
	settings.midiReceiveChannel=-1;
	currentPreset.steppedParameters[spAssignerMonoMode]=mUnisonLow;
	currentPreset.steppedParameters[spBenderSemitones]=5;
	currentPreset.steppedParameters[spBenderTarget]=modPitch;
	currentPreset.steppedParameters[spFilEnvExpo]=1;
	currentPreset.steppedParameters[spAmpEnvExpo]=1;
	currentPreset.continuousParameters[cpAmpVelocity]=UINT16_MAX/2;
	currentPreset.continuousParameters[cpFilVelocity]=0;
	
	// init
	
	scanner_init();
	display_init();
	synth_init();
	potmux_init();
	tuner_init();
	assigner_init();
	uart_init();
	arp_init();
	
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

	lfo_init(&p600.lfo);

	// state
		
		// initial input state
	
	scanner_update(1);
	
	for(p600Pot_t p=ppMixer;p<=ppFreqBFine;++p)
		potmux_update(1,1);

		// load stuff from storage
	
	int8_t settingsOk;
	settingsOk=settings_load();

	if(settingsOk && settings.presetBank!=pbkManual)
	{
		p600.presetDigitInput=pdiLoadDecadeDigit;
		p600.presetModified=0;
		preset_loadCurrent(settings.presetNumber);
	}
		
		// tune when settings are bad
	
	if(!settingsOk)
		tuner_tuneSynth();

		// yep
	
	refreshFullState();
	
	// a nice welcome message, and we're ready to go :)
	
	sevenSeg_scrollText("GliGli's P600 upgrade "VERSION,1);
}

void p600_update(void)
{
	int8_t i,wheelChange,wheelUpdate;
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
	potmux_update(1,1);
	potmux_update(1,1);
	potmux_update(1,1);
	potmux_update(1,1);
	
	// act on pot change
	
	refreshPresetPots(settings.presetBank==pbkManual);

	if(potmux_lastChanged()!=ppNone)
	{
		// display last changed pot value
		p600.manualDisplayedPot=potmux_lastChanged();
	}

	// has to stay outside of previous if, so that finer pot values changes can also be displayed
	refreshSevenSeg();

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

		// lfo
		
		lfo_setCVs(&p600.lfo,
				currentPreset.continuousParameters[cpLFOFreq],
				satAddU16U16(currentPreset.continuousParameters[cpLFOAmt],
					potmux_getValue(ppModWheel)>>currentPreset.steppedParameters[spModwheelShift]));
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
		
		p600.glideAmount=(UINT16_MAX-currentPreset.continuousParameters[cpGlide])>>5; // 11bit glide
		p600.gliding=p600.glideAmount<2000;
		
		// arp
		
		arp_setSpeed(potmux_getValue(ppSpeed));
		if (arp_getMode()!=amOff)
			p600.gliding=0;
		
		break;
	}

	// CV computations

		// bender
	
	wheelChange=potmux_hasChanged(ppPitchWheel);
	wheelUpdate=wheelChange || bendChangeStart+TICKER_1S>p600.ticker;
	
	if(wheelUpdate)
	{
		computeBenderCVs();

		// volume bending
		synth_setCV(pcMVol,satAddU16S16(potmux_getValue(ppMVol),p600.benderVolumeCV),SYNTH_FLAG_IMMEDIATE);
		
		if(wheelChange)
			bendChangeStart=p600.ticker;
	}

		// tuned CVs

	computeTunedCVs(wheelUpdate);
}

void p600_uartInterrupt(void)
{
	uart_update();
}

// 2Khz
void p600_timerInterrupt(void)
{
	int32_t va,vf;
	int16_t pitchLfoVal,filterLfoVal,filEnvAmt,oscEnvAmt;
	int8_t v,hz63,polyMask,monoGlidingMask;

	static uint8_t frc=0;

	// lfo
	
	lfo_update(&p600.lfo);
	
	pitchLfoVal=filterLfoVal=0;
	
	if(currentPreset.steppedParameters[spLFOTargets]&mtPitch)
		pitchLfoVal=p600.lfo.output;

	if(currentPreset.steppedParameters[spLFOTargets]&mtFilter)
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
		oscEnvAmt=va;		
	}
	
	// per voice stuff
	
	polyMask=(assigner_getMode()==mPoly)?0xff:0x00;
	monoGlidingMask=((assigner_getMode()==mMonoLow || assigner_getMode()==mMonoHigh) && p600.gliding)?0x00:0xff;
	
	if(!polyMask)
	{
		// in any mono mode, env is always P600_MONO_ENV
		adsr_update(&p600.filEnvs[P600_MONO_ENV]);
		adsr_update(&p600.ampEnvs[P600_MONO_ENV]);
	}
	
		// P600_VOICE_COUNT calls
	refreshVoice(0,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,polyMask);
	refreshVoice(1,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,polyMask);
	refreshVoice(2,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,polyMask);
	refreshVoice(3,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,polyMask);
	refreshVoice(4,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,polyMask);
	refreshVoice(5,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal,monoGlidingMask,polyMask);
	
	// slower updates

	hz63=(frc&0x1c)==0;	

	switch(frc&0x03) // 4 phases, each 500hz
	{
	case 0:
		refreshPulseWidth(currentPreset.steppedParameters[spLFOTargets]&mtPW);
		break;
	case 1:
		if(arp_getMode()!=amOff)
		{
			arp_update();
		}
		else if(p600.gliding)
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

		// ticker inc
		++p600.ticker;
		break;
	case 3:
		scanner_update(hz63);
		display_update(hz63);
		break;
	}
	
	++frc;
}

void p600_buttonEvent(p600Button_t button, int pressed)
{
	// button press might change current preset

	refreshPresetButton(button);		

	// tuning

	if(!pressed && button==pbTune)
	{
		tuner_tuneSynth();

		// tuner will thrash state
		refreshFullState();
	}
	
	// arp
	
	if(pressed && button==pbArpUD)
	{
		arp_setMode((arp_getMode()==amUpDown)?amOff:amUpDown,arp_getHold());
	}
	else if(pressed && button==pbArpAssign)
	{
		switch(arp_getMode())
		{
		case amOff:
		case amUpDown:
			arp_setMode(amAssign,arp_getHold());
			break;
		case amAssign:
			arp_setMode(amRandom,arp_getHold());
			break;
		case amRandom:
			arp_setMode(amOff,arp_getHold());
			break;
		}
	}
	
	if(arp_getMode()!=amOff && pressed && button==pbRecord)
	{
		arp_setMode(arp_getMode(),!arp_getHold());
		return; // override normal record action
	}
	
	// preset mode
	
	if(pressed && button==pbPreset)
	{
		settings.presetBank=(settings.presetBank+1)%2; //TODO: second preset bank, how to store?
		settings_save();		

		if(settings.presetBank!=pbkManual)
		{
			preset_loadCurrent(settings.presetNumber);
			p600.presetModified=0;
			refreshFullState();
		}

		p600.presetDigitInput=(settings.presetBank==pbkManual)?pdiNone:pdiLoadDecadeDigit;
	}
	
	if(pressed && button==pbRecord)
	{
		if(p600.presetDigitInput==pdiStoreDecadeDigit)
		{
			// cancel record
			p600.presetDigitInput=(settings.presetBank==pbkManual)?pdiNone:pdiLoadDecadeDigit;
		}
		else
		{
			// ask for digit
			p600.presetDigitInput=pdiStoreDecadeDigit;
		}
	}
	
	if(p600.presetDigitInput!=pdiNone)
	{
		// preset number input 
		
		if(pressed && button>=pb0 && button<=pb9)
		{
			switch(p600.presetDigitInput)
			{
			case pdiLoadDecadeDigit:
				p600.presetAwaitingNumber=button-pb0;
				p600.presetDigitInput=pdiLoadUnitDigit;
				break;
			case pdiStoreDecadeDigit:
				p600.presetAwaitingNumber=button-pb0;
				p600.presetDigitInput=pdiStoreUnitDigit;
				break;
			case pdiLoadUnitDigit:
			case pdiStoreUnitDigit:
				p600.presetAwaitingNumber=p600.presetAwaitingNumber*10+(button-pb0);

				// store?
				if(p600.presetDigitInput==pdiStoreUnitDigit)
				{
					preset_saveCurrent(p600.presetAwaitingNumber);
				}

				// always try to load/reload preset
				if(preset_loadCurrent(p600.presetAwaitingNumber))
				{
					settings.presetNumber=p600.presetAwaitingNumber;
					p600.presetModified=0;
					settings_save();		
	
					refreshFullState();
				}

				p600.presetAwaitingNumber=-1;
				p600.presetDigitInput=(settings.presetBank==pbkManual)?pdiNone:pdiLoadDecadeDigit;
				break;
			default:
				;
			}
		}
	}
	else
	{
		// assigner

		if((pressed && button==pb0))
		{
			currentPreset.steppedParameters[spAssignerMonoMode]=(currentPreset.steppedParameters[spAssignerMonoMode]+1)%(mMonoHigh+1);

			refreshAssignerSettings();
			sevenSeg_scrollText(assigner_modeName(currentPreset.steppedParameters[spAssignerMonoMode]),1);
		}

		if(button==pbUnison)
		{
			sevenSeg_scrollText(assigner_modeName(assigner_getMode()),1);
		}

		// lfo

		if((pressed && (button>=pb1 && button<=pb2)) || button==pbLFOShape)
		{
			uint8_t shpA,shp,spd,vA,v;
			
			shp=button==pbLFOShape;
			shpA=button==pb1;
			spd=button==pb2;

			if(shpA)
			{
				v=currentPreset.steppedParameters[spLFOShape];

				vA=v>>1;
				v&=1;
				vA=(vA+1)%3;
				
				currentPreset.steppedParameters[spLFOShape]=(vA<<1)|v;
			}
			
			if(spd)
				currentPreset.steppedParameters[spLFOShift]=(currentPreset.steppedParameters[spLFOShift]+1)%3;

			refreshLfoSettings(shp||shpA,spd);
		}

		// modwheel

		if((pressed && button==pb3))
		{
			const char * s[6]={"Full","","Half","","Min",""};
			currentPreset.steppedParameters[spModwheelShift]=(currentPreset.steppedParameters[spModwheelShift]+2)%6;
			sevenSeg_scrollText(s[currentPreset.steppedParameters[spModwheelShift]],1);
		}

		// envs

		if(pressed && (button>=pb4 && button<=pb5))
		{
			steppedParameter_t param;
			uint8_t merged;

			param=(button==pb5)?spFilEnvExpo:spAmpEnvExpo;
			
			merged=currentPreset.steppedParameters[param]+currentPreset.steppedParameters[param+1]*2;
			
			merged=(merged+1)%4;
			
			currentPreset.steppedParameters[param]=merged&1;
			currentPreset.steppedParameters[param+1]=(merged>>1)&1;

			refreshEnvSettings(param==spFilEnvExpo,1);
		}

		// pitch mode

		if(pressed && button==pb6)
		{
			const char * s[2]={"Free","Chromatic"};
			currentPreset.steppedParameters[spChromaticPitch]^=1;
			sevenSeg_scrollText(s[currentPreset.steppedParameters[spChromaticPitch]],1);
		}


		// bender

		if(pressed && (button>=pb7 && button<=pb9))
		{
			const char * s=NULL;

			if(button==pb7)
			{
				switch(currentPreset.steppedParameters[spBenderSemitones])
				{
				case 3:
					currentPreset.steppedParameters[spBenderSemitones]=5;
					s="5th";
					break;
				case 5:
					currentPreset.steppedParameters[spBenderSemitones]=12;
					s="Oct";
					break;
				case 12:
					currentPreset.steppedParameters[spBenderSemitones]=3;
					s="3rd";
					break;
				}
			}

			if(button==pb8)
			{
				currentPreset.steppedParameters[spBenderTarget]=(currentPreset.steppedParameters[spBenderTarget]+1)%(modVolume+1);
				s=modulationName(currentPreset.steppedParameters[spBenderTarget]);
			}

			if(button==pb9)
			{
				settings.benderMiddle=potmux_getValue(ppPitchWheel);
				s="Calibrated";
			}

			// clear bender CVs, force recompute
			memset(&p600.benderCVs,0,sizeof(p600.benderCVs));
			computeBenderCVs();

			sevenSeg_scrollText(s,1);
		}
	}
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

void p600_assignerEvent(uint8_t note, int8_t gate, int8_t voice, uint16_t velocity)
{
	int8_t env;
	uint16_t velAmt;
	
	// prepare CVs
	
	computeTunedCVs(1);
	
	// set gates
	
	env=voice;
	if(assigner_getMode()!=mPoly)
		env=P600_MONO_ENV;

		// don't retrigger gate, unless we're arpeggiating
	
	if(p600.ampEnvs[env].gate!=gate || arp_getMode()!=amOff)
	{
		adsr_setGate(&p600.filEnvs[env],gate);
		adsr_setGate(&p600.ampEnvs[env],gate);
	}

	// handle velocity
	
	if(gate)
	{
		velAmt=currentPreset.continuousParameters[cpFilVelocity];
		adsr_setCVs(&p600.filEnvs[env],0,0,0,0,(UINT16_MAX-velAmt)+scaleU16U16(velocity,velAmt),0x10);
		velAmt=currentPreset.continuousParameters[cpAmpVelocity];
		adsr_setCVs(&p600.ampEnvs[env],0,0,0,0,(UINT16_MAX-velAmt)+scaleU16U16(velocity,velAmt),0x10);
	}
	
	// prepare voices on gate on

	if(gate && voice>=0)
	{
		// pre-set rough pitches, to avoid tiny portamento-like glitches
		// when the voice actually starts

		synth_setCV(pcOsc1A+voice,p600.oscANoteCV[voice],SYNTH_FLAG_IMMEDIATE);
		synth_setCV(pcOsc1B+voice,p600.oscBNoteCV[voice],SYNTH_FLAG_IMMEDIATE);
		synth_setCV(pcFil1+voice,p600.filterNoteCV[voice],SYNTH_FLAG_IMMEDIATE);
		
		// kick-start the VCA, in case we need a sharp attack
		
		if(p600.ampEnvs[env].attackCV<512)
			synth_setCV(pcAmp1+voice,UINT16_MAX,SYNTH_FLAG_IMMEDIATE);
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
	midi_device_input(&p600.midi,1,&data);
}
