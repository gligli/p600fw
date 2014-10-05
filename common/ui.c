////////////////////////////////////////////////////////////////////////////////
// User interface
////////////////////////////////////////////////////////////////////////////////

#include "ui.h"
#include "storage.h"
#include "arp.h"
#include "scanner.h"
#include "display.h"
#include "potmux.h"
#include "midi.h"

const struct uiParam_s uiParameters[] =
{
	/*first press*/
	/*0*/ {.type=ptCont,.number=cpSeqArpClock,.name="speed"},
	/*1*/ {.type=ptCust,.number=0,.name="lfo shape",.values={"pulse-tri","rand-sin","noise-saw"}},
	/*2*/ {.type=ptCont,.number=cpVibFreq,.name="Vib spd"},
	/*3*/ {.type=ptCont,.number=cpVibAmt,.name="Vib amt"},
	/*4*/ {.type=ptCont,.number=cpModDelay,.name="mod delay"},
	/*5*/ {.type=ptCust,.number=2,.name="amp shape",.values={"fast-exp","fast-lin","slo-exp","slo-lin"}},
	/*6*/ {.type=ptStep,.number=spBenderTarget,.name="bend tgt",.values={"off","Vco","Vcf","Vol"}},
	/*7*/ {.type=ptCont,.number=cpGlide,.name="glide"},
	/*8*/ {.type=ptCont,.number=cpUnisonDetune,.name="detune"},
	/*9*/ {.type=ptCont,.number=cpAmpVelocity,.name="amp Velo"},
	/*second press*/
	/*0*/ {.type=ptCont,.number=cpSeqArpClock,.name="speed"},
	/*1*/ {.type=ptCust,.number=1,.name="lfo tgt",.values={"ab","a","b"}},
	/*2*/ {.type=ptStep,.number=spLFOShift,.name="lfo range",.values={"low","high"}},
	/*3*/ {.type=ptCust,.number=5,.name="mod range",.values={"min","low","high","full"}},
	/*4*/ {.type=ptStep,.number=spModwheelTarget,.name="mod tgt",.values={"lfo","Vib"}},
	/*5*/ {.type=ptCust,.number=3,.name="fil shape",.values={"fast-exp","fast-lin","slo-exp","slo-lin"}},
	/*6*/ {.type=ptCust,.number=4,.name="bend range",.values={"2nd","3rd","5th","Oct"}},
	/*7*/ {.type=ptStep,.number=spAssignerPriority,.name="prio",.values={"last","low","high"}},	
	/*8*/ {.type=ptStep,.number=spChromaticPitch,.name="pitch",.values={"free","semitone","octaVe"}},
	/*9*/ {.type=ptCont,.number=cpFilVelocity,.name="fil Velo"},
};

struct ui_s ui;

extern void refreshFullState(void);
extern void refreshPresetMode(void);
extern void computeBenderCVs(void);

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
			(currentPreset.steppedParameters[spLFOTargets]&(mtOnlyA|mtOnlyB)) | // keep those as-is
			(scanner_buttonState(pbLFOFreq)?mtVCO:0) |
			(scanner_buttonState(pbLFOPW)?mtPW:0) |
			(scanner_buttonState(pbLFOFil)?mtVCF:0);
		break;
	case pbFilFull:
	case pbFilHalf:
		currentPreset.steppedParameters[spTrackingShift]=
			(scanner_buttonState(pbFilHalf)?1:0) |
			(scanner_buttonState(pbFilFull)?2:0);
		break;
	default:
		change=0;
	}
	
	if(change)
	{
		ui.presetModified=1;
		refreshFullState();
	}
}

LOWERCODESIZE void refreshAllPresetButtons(void)
{
	p600Button_t b;
	for(b=pbASqr;b<=pbUnison;++b)
		refreshPresetButton(b);
}

static LOWERCODESIZE void handleMiscAction(p600Button_t button)
{
	const char * chs[17]={"omni","ch1","ch2","ch3","ch4","ch5","ch6","ch7","ch8","ch9","ch10","ch11","ch12","ch13","ch14","ch15","ch16"};
	static int8_t voice=0;
	char s[20];
	
	

	if(button==pb1) // midi receive channel
	{
		settings.midiReceiveChannel=((settings.midiReceiveChannel+2)%17)-1;
		settings_save();
		
		strcpy(s,chs[settings.midiReceiveChannel+1]);
		strcat(s," recv");
		
		sevenSeg_scrollText(s,1);
	}
	else if(button==pb2) // midi send channel
	{
		settings.midiSendChannel=(settings.midiSendChannel+1)%16;
		settings_save();
		
		strcpy(s,chs[settings.midiSendChannel+1]);
		strcat(s," send");
		
		sevenSeg_scrollText(s,1);
	}
	else if(button==pb3) // pitch wheel calibration
	{
		settings.benderMiddle=potmux_getValue(ppPitchWheel);
		settings_save();

		synth_updateBender(); // immediate update

		sevenSeg_scrollText("bender calibrated",1);
	}
	else if(button==pb4) // voice selection
	{
		voice=(voice+1)%SYNTH_VOICE_COUNT;

		strcpy(s,"Vc-");
		s[2]='1'+voice;
		sevenSeg_scrollText(s,1);
	}
	else if(button==pb5) // selected voice defeat
	{
		if(settings.voiceMask&(1<<voice))
		{
			strcpy(s,"Vc- off");
			settings.voiceMask&=~(1<<voice);
		}
		else
		{
			strcpy(s,"Vc- on");
			settings.voiceMask|=(1<<voice);
		}

		settings_save();

		s[2]='1'+voice;
		sevenSeg_scrollText(s,1);
		refreshFullState();
	}
	else if(button==pb6) // preset dump
	{
		midi_dumpPresets();
		sevenSeg_scrollText("presets dumped",1);
		refreshPresetMode();
		refreshFullState();
	}
	else if(button==pb8) // sync mode
	{
		settings.syncMode=(settings.syncMode+1)%3;
		settings_save();
		
		switch(settings.syncMode)
		{
			case smInternal:
				sevenSeg_scrollText("Int sync",1);
				break;
			case smMIDI:
				sevenSeg_scrollText("Midi sync",1);
				break;
			case smTape:
				sevenSeg_scrollText("tape sync",1);
				break;
		}

		refreshFullState();
	}
	else if(button==pb9) // spread
	{
		settings.spread=(settings.spread+1)%2;
		settings_save();
		
		if(settings.spread)
		{
			sevenSeg_scrollText("spread on",1);
		}
		else
		{
			sevenSeg_scrollText("spread off",1);
		};
		
		refreshFullState();
	}
	else if(button==pb0) // reset to a basic patch
	{
		preset_loadDefault(1);
		ui.presetModified=1;
		sevenSeg_scrollText("basic patch",1);
		refreshFullState();
	}
}

static LOWERCODESIZE void setCustomParameter(int8_t num, int32_t data)
{
	int8_t br[]={2,3,5,12};
	int8_t mr[]={5,3,1,0};

	switch(num)
	{
	case 0: // lfo shape 
		currentPreset.steppedParameters[spLFOShape]=(currentPreset.steppedParameters[spLFOShape]&1) | (data<<1);
		break;
	case 1: // lfo tgt
		currentPreset.steppedParameters[spLFOTargets]&=~(mtOnlyA|mtOnlyB);
		if(data==1)
			currentPreset.steppedParameters[spLFOTargets]|=mtOnlyA;
		else if(data==2)
			currentPreset.steppedParameters[spLFOTargets]|=mtOnlyB;
		break;					
	case 2: // amp shape
		currentPreset.steppedParameters[spAmpEnvExpo]=1-(data&1);
		currentPreset.steppedParameters[spAmpEnvSlow]=(data&2)>>1;
		break;
	case 3: // fil shape 
		currentPreset.steppedParameters[spFilEnvExpo]=1-(data&1);
		currentPreset.steppedParameters[spFilEnvSlow]=(data&2)>>1;
		break;
	case 4: // bend range 
		currentPreset.steppedParameters[spBenderSemitones]=br[data];
		break;
	case 5: // mod range
		currentPreset.steppedParameters[spModwheelShift]=mr[data];
		break;
	}
}

static LOWERCODESIZE void displayUIParameter(int8_t num)
{
	int8_t i;
	char s[20];
	const struct uiParam_s * prm = &uiParameters[ui.activeParamIdx];

	ui_setNoActivePot();
	
	strcpy(s,prm->name);
	strcat(s," = ");
	
	switch(prm->type)
	{
	case ptCont:
		ui.lastActivePotValue=ui.adjustedLastActivePotValue=currentPreset.continuousParameters[prm->number];
		break;
	case ptStep:
		strcat(s,prm->values[currentPreset.steppedParameters[prm->number]]);
		break;
	case ptCust:
		// reverse lookup for uiParam value (assumes only steppedParameters will be modified)
		memcpy(tempBuffer,currentPreset.steppedParameters,sizeof(currentPreset.steppedParameters));
		for(i=0;i<4;++i)
		{
			setCustomParameter(prm->number,i);
			if(!memcmp(tempBuffer,currentPreset.steppedParameters,sizeof(currentPreset.steppedParameters)))
			{
				strcat(s,prm->values[i]);
				break;
			}
		}
		break;
	}
	
	sevenSeg_scrollText(s,1);
}

static LOWERCODESIZE void handleSynthPage(p600Button_t button)
{
	int8_t prev,new;
	
	prev=ui.activeParamIdx;
	
	if(button>=pb0 && button<=pb9)
	{
		new=button-pb0;
		
		if (prev==new)
			ui.activeParamIdx+=10;
		else if (prev==new+10)
			ui.activeParamIdx-=10;
		else
			ui.activeParamIdx=new;
		ui.previousData=-1;
	}
	
	if(ui.activeParamIdx!=prev)
	{
		// display param name + value
		
		displayUIParameter(ui.activeParamIdx);
		
		// save manual preset
		
		if(!settings.presetMode)
			preset_saveCurrent(MANUAL_PRESET_PAGE);
	}
}

void ui_setNoActivePot(void)
{
	potmux_resetChanged();
	ui.lastActivePot=ppNone;
	ui.lastActivePotValue=-1;
}

FORCEINLINE void ui_setPresetModified(int8_t modified)
{
	ui.presetModified=modified;
}

FORCEINLINE int8_t ui_isPresetModified(void)
{
	return ui.presetModified;
}

void ui_checkIfDataPotChanged(void)
{
	ui.lastActivePot = potmux_lastChanged() != ppNone ? potmux_lastChanged() : ui.lastActivePot;
	
	if(ui.lastActivePot!=ppSpeed)
		return;
	
	// handle our "data" pot
	
	if(ui.activeParamIdx>=0)
	{
		int32_t data,valCount;
		struct uiParam_s prm;
		
		data=potmux_getValue(ppSpeed);
		
		if(data==ui.lastActivePotValue) // prevent slowdown caused by unwanted updates
			return;
		
		prm=uiParameters[ui.activeParamIdx];
		
		switch(prm.type)
		{
		case ptCont:
			currentPreset.continuousParameters[prm.number]=data;
			break;
		case ptStep:
		case ptCust:
			ui_setNoActivePot();
			
			valCount=0;
			while(valCount<4 && prm.values[valCount]!=NULL)
				++valCount;
			
			data=(data*valCount)>>16;

			if(data!=ui.previousData)
				sevenSeg_scrollText(prm.values[data],1);

			if(prm.type==ptStep)
			{
				currentPreset.steppedParameters[prm.number]=data;
			}
			else
			{
				setCustomParameter(prm.number,data);
			}

			ui.previousData=data;
			break;
		}
		ui.presetModified=1;
		
		refreshFullState();
	}
}


void LOWERCODESIZE ui_handleButton(p600Button_t button, int pressed)
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
		arp_setMode(arp_getMode(),arp_getHold()?0:1);
		return; // override normal record action
	}

	// assigner
	
	if(button==pbUnison)
	{
		if(pressed)
		{
			assigner_latchPattern();
		}
		else
		{
			assigner_setPoly();
		}
		assigner_getPattern(currentPreset.voicePattern,NULL);

		// save manual preset
		
		if(!settings.presetMode)
			preset_saveCurrent(MANUAL_PRESET_PAGE);
	}

	// digit buttons use
	
	if(pressed && button==pbToTape && settings.presetMode)
	{
		if(ui.digitInput!=diSynth)
		{
			ui.digitInput=diSynth;
		}
		else
		{
			ui.digitInput=diLoadDecadeDigit;
		}
	}

	// keyboard transposition
	
	if(button==pbFromTape)
		ui.isTransposing=pressed;
	
	// modes 
	
	if(pressed)
	{
		if(scanner_buttonState(pbFromTape))
		{
			handleMiscAction(button);
		}
		else if(button==pbPreset)
		{
			// save manual preset
			if(!settings.presetMode)
				preset_saveCurrent(MANUAL_PRESET_PAGE);

			settings.presetMode=settings.presetMode?0:1;
			settings_save();
			refreshPresetMode();
			refreshFullState();
		}
		else if(button==pbRecord)
		{
			if(ui.digitInput==diStoreDecadeDigit)
			{
				// cancel record
				ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth;
			}
			else
			{
				// ask for digit
				ui.digitInput=diStoreDecadeDigit;
			}
		}
		else if(ui.digitInput==diSynth)
		{
			handleSynthPage(button);
		}
		else if(ui.digitInput>=diLoadDecadeDigit && button>=pb0 && button<=pb9)
		{
			// preset number input 
			switch(ui.digitInput)
			{
			case diLoadDecadeDigit:
				ui.presetAwaitingNumber=button-pb0;
				ui.digitInput=diLoadUnitDigit;
				break;
			case diStoreDecadeDigit:
				ui.presetAwaitingNumber=button-pb0;
				ui.digitInput=diStoreUnitDigit;
				break;
			case diLoadUnitDigit:
			case diStoreUnitDigit:
				ui.presetAwaitingNumber=ui.presetAwaitingNumber*10+(button-pb0);

				// store?
				if(ui.digitInput==diStoreUnitDigit)
				{
					preset_saveCurrent(ui.presetAwaitingNumber);
				}

				// always try to load/reload preset
				if(preset_loadCurrent(ui.presetAwaitingNumber))
				{
					settings.presetNumber=ui.presetAwaitingNumber;
					settings_save();		
				}

				ui.presetAwaitingNumber=-1;
				
				refreshPresetMode();
				break;
			default:
				;
			}
		}
	}
	
	// we might have changed state
	
	refreshFullState();
}

void ui_init(void)
{
	memset(&ui,0,sizeof(ui));
		
	ui.digitInput=diSynth;
	ui.presetAwaitingNumber=-1;
	ui.lastActivePot=ppNone;
	ui.lastActivePotValue=-1;
	ui.presetModified=1;
	ui.activeParamIdx=-1;
}
