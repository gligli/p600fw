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
#include "import.h"

#define MIDI_BASE_STEPPED_CC 48
#define MIDI_BASE_COARSE_CC 16
#define MIDI_BASE_FINE_CC 80
#define MIDI_BASE_NOTE 12

#define TICKER_1S 500

#define MAX_SYSEX_SIZE 512

static void sysexSend(uint8_t command, int16_t size);

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

volatile uint32_t currentTick=0; // 500hz
	
static struct
{
	struct adsr_s filEnvs[P600_VOICE_COUNT];
	struct adsr_s ampEnvs[P600_VOICE_COUNT];

	struct lfo_s lfo;
	
	struct preset_s manualPreset;
	
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
	
	p600Pot_t lastActivePot;
	uint8_t manualActivePotValue;
	
	enum {diSynth,diMisc,diLoadDecadeDigit,diStoreDecadeDigit,diLoadUnitDigit,diStoreUnitDigit} digitInput;
	int8_t presetAwaitingNumber;
	int8_t presetModified;
	
	uint8_t sysexBuffer[MAX_SYSEX_SIZE];
	int16_t sysexSize;
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
	int8_t v;

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
		assigner_setPattern(currentPreset.voicePattern);
	else
		assigner_setPolyPattern();
	
	assigner_setVoiceMask(settings.voiceMask);
	assigner_setPriority(currentPreset.steppedParameters[spAssignerPriority]);
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
		srandom(currentTick);
	
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
	if(p600.digitInput<diLoadDecadeDigit)
	{
		uint8_t v=p600.manualActivePotValue;
		sevenSeg_setNumber(v);
		led_set(plDot,v>99,v>199);
	}
	else
	{
		if(p600.digitInput!=diLoadDecadeDigit)
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
	led_set(plToTape,p600.digitInput==diSynth && settings.presetBank!=pbkManual,0);
	led_set(plFromTape,p600.digitInput==diMisc,0);
	
	if(arp_getMode()!=amOff)
	{
		led_set(plRecord,arp_getHold(),0);
		led_set(plArpUD,arp_getMode()==amUpDown,0);
		led_set(plArpAssign,arp_getMode()!=amUpDown,arp_getMode()==amRandom);
	}
	else
	{
		led_set(plRecord,p600.digitInput==diStoreDecadeDigit,p600.digitInput==diStoreDecadeDigit);
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
		if(force || continuousParameterToPot[cp]==p600.lastActivePot || potmux_hasChanged(continuousParameterToPot[cp]))
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

static void refreshPresetMode(void)
{
	if(settings.presetBank!=pbkManual)
	{
		preset_loadCurrent(settings.presetNumber);
	}
	else
	{
		currentPreset=p600.manualPreset;
	}

	refreshFullState();

	p600.lastActivePot=ppNone;
	p600.presetModified=0;
	p600.digitInput=(settings.presetBank==pbkManual)?diSynth:diLoadDecadeDigit;
}

static FORCEINLINE void refreshVoice(int8_t v,int16_t oscEnvAmt,int16_t filEnvAmt,int16_t pitchLfoVal,int16_t filterLfoVal)
{
	int32_t va,vb,vf;
	uint16_t envVal;
	int8_t assigned;
	
	assigned=assigner_getAssignment(v,NULL);
	
	BLOCK_INT
	{
		if(assigned)
		{
			// handle envs update

			adsr_update(&p600.filEnvs[v]);
			adsr_update(&p600.ampEnvs[v]);

			// compute CVs & apply them

			envVal=p600.filEnvs[v].output;

			va=vb=pitchLfoVal;

			va+=scaleU16S16(envVal,oscEnvAmt);	
			va+=p600.oscANoteCV[v];

			synth_setCV32Sat_FastPath(pcOsc1A+v,va);

			vb+=p600.oscBNoteCV[v];
			synth_setCV32Sat_FastPath(pcOsc1B+v,vb);

			vf=filterLfoVal;
			vf+=scaleU16S16(envVal,filEnvAmt);
			vf+=p600.filterNoteCV[v];
			synth_setCV32Sat_FastPath(pcFil1+v,vf);

			synth_setCV_FastPath(pcAmp1+v,p600.ampEnvs[v].output);
		}
		else
		{
			synth_setCV_FastPath(pcAmp1+v,0);
		}
	}
}

static LOWERCODESIZE void handleSynthPage(p600Button_t button, int pressed)
{
	// assigner

	if((pressed && button==pb0))
	{
		currentPreset.steppedParameters[spAssignerPriority]=(currentPreset.steppedParameters[spAssignerPriority]+1)%(apHigh+1);

		refreshAssignerSettings();
		sevenSeg_scrollText(assigner_priorityName[currentPreset.steppedParameters[spAssignerPriority]],1);
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

	if(pressed && (button==pb7 || button==pb8))
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

		// clear bender CVs, force recompute
		memset(&p600.benderCVs,0,sizeof(p600.benderCVs));
		computeBenderCVs();

		sevenSeg_scrollText(s,1);
	}
}

static LOWERCODESIZE void handleMiscPage(p600Button_t button, int pressed)
{
	const char * chs[17]={"omni","ch1","ch2","ch3","ch4","ch5","ch6","ch7","ch8","ch9","ch10","ch11","ch12","ch13","ch14","ch15","ch16"};
	
	// midi receive channel

	if(pressed && button==pb1)
	{
		char s[20];
		
		settings.midiReceiveChannel=((settings.midiReceiveChannel+2)%17)-1;
		settings_save();
		
		strcpy(s,chs[settings.midiReceiveChannel+1]);
		strcat(s," midi recv");
		
		sevenSeg_scrollText(s,1);
	}
	
	if(pressed && button==pb2)
	{
		settings.benderMiddle=potmux_getValue(ppPitchWheel);
		settings_save();

		// clear bender CVs, force recompute
		memset(&p600.benderCVs,0,sizeof(p600.benderCVs));
		computeBenderCVs();

		sevenSeg_scrollText("bender calibrated",1);
	}
	
	if(pressed && button==pb3)
	{
		int8_t i;
		int16_t size=0;
		
		for(i=0;i<100;++i)
		{
			if(preset_loadCurrent(i))
			{
				storage_export(i,p600.sysexBuffer,&size);
				sysexSend(SYSEX_COMMAND_BANK_A,size);
			}
			
			sevenSeg_scrollText("done",1);
		}
		
		refreshPresetMode();
	}
	
}

////////////////////////////////////////////////////////////////////////////////
// MIDI
////////////////////////////////////////////////////////////////////////////////

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

	if(control==0 && value<=pbkA && settings.presetBank!=value) // coarse bank #
	{
		// save manual preset
		if (settings.presetBank==pbkManual)
			p600.manualPreset=currentPreset;
		
		settings.presetBank=value;
		settings_save();
		refreshPresetMode();
		refreshSevenSeg();
	}
	
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
		p600.presetModified=1;	
	}

	if(p600.presetModified)
		refreshFullState();
}

void midi_progChangeEvent(MidiDevice * device, uint8_t channel, uint8_t program)
{
	if(!midiFilterChannel(channel))
		return;

	if(settings.presetBank!=pbkManual && program<100  && program!=settings.presetNumber)
	{
		if(preset_loadCurrent(program))
		{
			settings.presetNumber=program;
			p600.lastActivePot=ppNone;
			p600.presetModified=0;
			settings_save();		
			refreshFullState();
		}
	}
}

static void sysexSend(uint8_t command, int16_t size)
{
	int16_t chunkCount,i;
	uint8_t chunk[4];
	
	chunkCount=((size-1)>>2)+1;
	
	uart_send(0xf0);
	uart_send(SYSEX_ID_0);
	uart_send(SYSEX_ID_1);
	uart_send(SYSEX_ID_2);
	uart_send(command);
	
	for(i=0;i<chunkCount;++i)
	{
		memcpy(chunk,&p600.sysexBuffer[i<<2],4);
		
		uart_send(chunk[0]&0x7f);
		uart_send(chunk[1]&0x7f);
		uart_send(chunk[2]&0x7f);
		uart_send(chunk[3]&0x7f);
		uart_send(((chunk[0]>>7)&1) | ((chunk[1]>>6)&2) | ((chunk[2]>>5)&4) | ((chunk[3]>>4)&8));
	}

	uart_send(0xf7);
}

int16_t sysexDescrambleBuffer(int16_t start)
{
	int16_t chunkCount,i,out;
	uint8_t b;
	
	chunkCount=((p600.sysexSize-start)/5)+1;
	out=start;

	for(i=0;i<chunkCount;++i)
	{
		memmove(&p600.sysexBuffer[out],&p600.sysexBuffer[i*5+start],4);
		
		b=p600.sysexBuffer[i*5+start+4];
		
		p600.sysexBuffer[out+0]|=(b&1)<<7;
		p600.sysexBuffer[out+1]|=(b&2)<<6;
		p600.sysexBuffer[out+2]|=(b&4)<<5;
		p600.sysexBuffer[out+3]|=(b&8)<<4;
		
		out+=4;
	}
	
	return out-start;
}

void sysexReceiveByte(uint8_t b)
{
	int16_t size;

	switch(b)
	{
	case 0xF0:
		p600.sysexSize=0;
		memset(p600.sysexBuffer,0,MAX_SYSEX_SIZE);
		break;
	case 0xF7:
		if(p600.sysexBuffer[0]==0x01 && p600.sysexBuffer[1]==0x02) // SCI P600 program dump
		{
			import_sysex(p600.sysexBuffer,p600.sysexSize);
		}
		else if(p600.sysexBuffer[0]==SYSEX_ID_0 && p600.sysexBuffer[1]==SYSEX_ID_1 && p600.sysexBuffer[2]==SYSEX_ID_2) // my sysex ID
		{
			// handle my sysex commands
			
			switch(p600.sysexBuffer[3])
			{
			case SYSEX_COMMAND_BANK_A:
				size=sysexDescrambleBuffer(4);
				storage_import(p600.sysexBuffer[4],&p600.sysexBuffer[5],size-1);
				break;
			}
		}

		p600.sysexSize=0;
		refreshFullState();
		break;
	default:
		if(p600.sysexSize>=MAX_SYSEX_SIZE)
		{
#ifdef DEBUG
			print("Warning: sysex buffer overflow\n");
#endif
			p600.sysexSize=0;
		}
		
		p600.sysexBuffer[p600.sysexSize++]=b;
	}
}

void midi_sysexEvent(MidiDevice * device, uint16_t count, uint8_t b0, uint8_t b1, uint8_t b2)
{
	if(p600.sysexSize)
		count=count-p600.sysexSize-1;
	
	if(count>0)
		sysexReceiveByte(b0);
	
	if(count>1)
		sysexReceiveByte(b1);

	if(count>2)
		sysexReceiveByte(b2);
}

////////////////////////////////////////////////////////////////////////////////
// P600 main code
////////////////////////////////////////////////////////////////////////////////

void p600_init(void)
{
	int8_t i;
	
	memset(&p600,0,sizeof(p600));
	memset(&settings,0,sizeof(settings));
	
	// defaults
	
	settings.benderMiddle=UINT16_MAX/2;
	settings.presetBank=pbkManual;
	settings.midiReceiveChannel=-1;
	settings.voiceMask=0x3f;
	p600.digitInput=diSynth;
	p600.presetAwaitingNumber=-1;
	p600.lastActivePot=ppNone;
	currentPreset.steppedParameters[spBenderSemitones]=5;
	currentPreset.steppedParameters[spBenderTarget]=modPitch;
	currentPreset.steppedParameters[spFilEnvExpo]=1;
	currentPreset.steppedParameters[spAmpEnvExpo]=1;
	currentPreset.continuousParameters[cpAmpVelocity]=UINT16_MAX/2;
	currentPreset.continuousParameters[cpFilVelocity]=0;
	for(i=0;i<P600_VOICE_COUNT;++i)
		currentPreset.voicePattern[i]=(i==0)?0:ASSIGNER_NO_NOTE;
	
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
	midi_register_progchange_callback(&p600.midi,midi_progChangeEvent);
	midi_register_sysex_callback(&p600.midi,midi_sysexEvent);
	
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_init(&p600.ampEnvs[i]);
		adsr_init(&p600.filEnvs[i]);
	}

	lfo_init(&p600.lfo);

	// state
		
		// initial input state
	
	scanner_update(1);
	potmux_update(POTMUX_POT_COUNT);

		// save manual, in case we load a patch
	
	p600.manualPreset=currentPreset;
	
		// load stuff from storage
	
	int8_t settingsOk;
	settingsOk=settings_load();

	if(settingsOk && settings.presetBank!=pbkManual)
	{
		p600.digitInput=diLoadDecadeDigit;
		p600.lastActivePot=ppNone;
		p600.presetModified=0;
		preset_loadCurrent(settings.presetNumber);
	}
		
		// tune when settings are bad
	
#ifndef DEBUG
	if(!settingsOk)
		tuner_tuneSynth();
#endif	

		// yep
	
	refreshFullState();
	
	// a nice welcome message, and we're ready to go :)
	
	sevenSeg_scrollText("GliGli's P600 upgrade "VERSION,1);
}

void p600_update(void)
{
	int8_t i,wheelChange,wheelUpdate;
	uint8_t potVal;
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
	{
		// display last changed pot value
		p600.lastActivePot=potmux_lastChanged();
	}

	refreshPresetPots(settings.presetBank==pbkManual);

	// has to stay outside of previous if, so that finer pot values changes can also be displayed
	
	potVal=potmux_getValue(p600.lastActivePot)>>8;
	if(potVal!=p600.manualActivePotValue)
	{
		p600.manualActivePotValue=potVal;
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
		
		p600.glideAmount=exponentialCourse(currentPreset.continuousParameters[cpGlide],11000.0f,2100.0f);
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

	computeTunedCVs(wheelUpdate);
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
	int16_t pitchLfoVal,filterLfoVal,filEnvAmt,oscEnvAmt;
	int8_t v,hz63;

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
	
		// P600_VOICE_COUNT calls
	refreshVoice(0,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal);
	refreshVoice(1,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal);
	refreshVoice(2,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal);
	refreshVoice(3,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal);
	refreshVoice(4,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal);
	refreshVoice(5,oscEnvAmt,filEnvAmt,pitchLfoVal,filterLfoVal);
	
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
		++currentTick;
		break;
	case 3:
		scanner_update(hz63);
		display_update(hz63);
		break;
	}
	
	++frc;
}

////////////////////////////////////////////////////////////////////////////////
// P600 internal events
////////////////////////////////////////////////////////////////////////////////

void LOWERCODESIZE p600_buttonEvent(p600Button_t button, int pressed)
{
	// button press might change current preset

	refreshPresetButton(button);		

	// tuning

	if(!pressed && button==pbTune)
	{
		tuner_tuneSynth();
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

	// assigner
	
	if(button==pbUnison)
	{
		if(pressed)
		{
			assigner_latchPattern();
			assigner_getPattern(currentPreset.voicePattern);
		}
		else
		{
			assigner_setPolyPattern();
		}
	}

	// digit buttons use
	
	if(pressed && button==pbToTape && settings.presetBank!=pbkManual)
	{
		if(p600.digitInput!=diSynth)
		{
			p600.digitInput=diSynth;
			sevenSeg_scrollText("sound settings",1);
		}
		else
		{
			p600.digitInput=diLoadDecadeDigit;
		}
	}
	else if(pressed && button==pbFromTape)
	{
		if(p600.digitInput!=diMisc)
		{
			p600.digitInput=diMisc;
			sevenSeg_scrollText("misc settings",1);
		}
		else
		{
			p600.digitInput=(settings.presetBank==pbkManual)?diSynth:diLoadDecadeDigit;
		}
	}

	// preset mode
	
	if(pressed && button==pbPreset)
	{
		// save manual preset
		if (settings.presetBank==pbkManual)
			p600.manualPreset=currentPreset;
		
		settings.presetBank=(settings.presetBank+1)%2; //TODO: second preset bank, how to store?
		settings_save();
		refreshPresetMode();
	}
	
	if(pressed && button==pbRecord)
	{
		if(p600.digitInput==diStoreDecadeDigit)
		{
			// cancel record
			p600.digitInput=(settings.presetBank==pbkManual)?diSynth:diLoadDecadeDigit;
		}
		else
		{
			// ask for digit
			p600.digitInput=diStoreDecadeDigit;
		}
	}
	
	if(p600.digitInput>=diLoadDecadeDigit)
	{
		// preset number input 
		
		if(pressed && button>=pb0 && button<=pb9)
		{
			switch(p600.digitInput)
			{
			case diLoadDecadeDigit:
				p600.presetAwaitingNumber=button-pb0;
				p600.digitInput=diLoadUnitDigit;
				break;
			case diStoreDecadeDigit:
				p600.presetAwaitingNumber=button-pb0;
				p600.digitInput=diStoreUnitDigit;
				break;
			case diLoadUnitDigit:
			case diStoreUnitDigit:
				p600.presetAwaitingNumber=p600.presetAwaitingNumber*10+(button-pb0);

				// store?
				if(p600.digitInput==diStoreUnitDigit)
				{
					preset_saveCurrent(p600.presetAwaitingNumber);
				}

				// always try to load/reload preset
				if(preset_loadCurrent(p600.presetAwaitingNumber))
				{
					settings.presetNumber=p600.presetAwaitingNumber;
					p600.lastActivePot=ppNone;
					p600.presetModified=0;
					settings_save();		
				}

				p600.presetAwaitingNumber=-1;
				p600.digitInput=(settings.presetBank==pbkManual)?diSynth:diLoadDecadeDigit;
				break;
			default:
				;
			}
		}
	}
	else
	{
		if(p600.digitInput==diSynth)
			handleSynthPage(button,pressed);
		else
			handleMiscPage(button,pressed);
	}
	
	// we might have changed state
	
	refreshFullState();
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
	int32_t v;
	
	// prepare CVs
	
	computeTunedCVs(1);
	
	// set gates (don't retrigger gate, unless we're arpeggiating)
	
	if(!legato || (arp_getMode()!=amOff))
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
	
		// prepare voices on gate on (don't do it in legato mode, it would glitch)
		
		if(!legato)
		{
			// pre-set rough pitches, to avoid tiny portamento-like glitches
			// when the voice actually starts

			synth_setCV(pcOsc1A+voice,p600.oscANoteCV[voice],SYNTH_FLAG_IMMEDIATE);
			synth_setCV(pcOsc1B+voice,p600.oscBNoteCV[voice],SYNTH_FLAG_IMMEDIATE);
		
			// preset & maybe kick-start the VCF, in case we need a sharp attack
		
			v=p600.filterNoteCV[voice];
			if(p600.filEnvs[voice].attackCV<512)
			{
				v+=currentPreset.continuousParameters[cpFilEnvAmt];
				v+=INT16_MIN;
			}

			synth_setCV32Sat(pcFil1+voice,v,SYNTH_FLAG_IMMEDIATE);
		
			// kick-start the VCA, in case we need a sharp attack
		
			if(p600.ampEnvs[voice].attackCV<512)
				synth_setCV(pcAmp1+voice,UINT16_MAX,SYNTH_FLAG_IMMEDIATE);
		}
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
