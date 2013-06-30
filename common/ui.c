////////////////////////////////////////////////////////////////////////////////
// User interface
////////////////////////////////////////////////////////////////////////////////

#include "ui.h"
#include "storage.h"
#include "arp.h"
#include "scanner.h"
#include "display.h"
#include "potmux.h"

const struct uiParam_s uiParameters[] =
{
	/*shortcuts*/
	/*1*/ {.type=ptCust,.number=0,.name="lfo shape",.values={"pulse-tri","rand-sin","noise-saw"}},
	/*2*/ {.type=ptStep,.number=spLFOShift,.name="lfo range",.values={"low","high"}},
	/*3*/ {.type=ptStep,.number=spBenderTarget,.name="bend tgt",.values={"Vco","Vcf","Vca","off"}},
	/*4*/ {.type=ptCont,.number=cpVibAmt,.name="Vib amt"},
	/*5*/ {.type=ptCont,.number=cpVibfreq,.name="Vib spd"},
	/*6*/ {.type=ptCont,.number=cpModDelay,.name="mod delay"},
	/*7*/ {.type=ptCont,.number=cpGlide,.name="glide"},
	/*8*/ {.type=ptCont,.number=cpSeqArpClock,.name="speed"},
	/*9*/ {.type=ptCont,.number=cpUnisonDetune,.name="detune"},
	/*misc*/
	{.type=ptCust,.number=1,.name="lfo tgt",.values={"ab","a","b"}},
	{.type=ptStep,.number=spAssignerPriority,.name="prio",.values={"last","low","high"}},
	{.type=ptCust,.number=2,.name="amp shape",.values={"fast-exp","fast-lin","slo-exp","slo-lin"}},
	{.type=ptCust,.number=3,.name="fil shape",.values={"fast-exp","fast-lin","slo-exp","slo-lin"}},
	{.type=ptStep,.number=spChromaticPitch,.name="pitch",.values={"free","semitone"}},
	{.type=ptCust,.number=4,.name="bend range",.values={"3rd","5th","Oct"}},
	{.type=ptStep,.number=spModwheelTarget,.name="mod tgt",.values={"lfo","Vib"}},
	{.type=ptStep,.number=spModwheelShift,.name="mod range",.values={"min","low","high","max"}},
};

struct ui_s ui;

extern void refreshFullState(void);
extern void refreshPresetMode(void);
extern void dumpPresets(void);
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
			(scanner_buttonState(pbLFOFreq)?mtVCO:0) +
			(scanner_buttonState(pbLFOPW)?mtPW:0) +
			(scanner_buttonState(pbLFOFil)?mtVCF:0);
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
		ui.presetModified=1;
		refreshFullState();
	}
}

static LOWERCODESIZE void handleMiscAction(p600Button_t button)
{
	const char * chs[17]={"omni","ch1","ch2","ch3","ch4","ch5","ch6","ch7","ch8","ch9","ch10","ch11","ch12","ch13","ch14","ch15","ch16"};
	
	// midi receive channel

	if(button==pb1)
	{
		char s[20];
		
		settings.midiReceiveChannel=((settings.midiReceiveChannel+2)%17)-1;
		settings_save();
		
		strcpy(s,chs[settings.midiReceiveChannel+1]);
		strcat(s," midi recv");
		
		sevenSeg_scrollText(s,1);
	}
	
	if(button==pb2)
	{
		settings.benderMiddle=potmux_getValue(ppPitchWheel);
		settings_save();

		computeBenderCVs();

		sevenSeg_scrollText("bender calibrated",1);
	}
	
	if(button==pb3)
	{
		dumpPresets();
		sevenSeg_scrollText("done",1);
		refreshPresetMode();
	}
	
}

static LOWERCODESIZE void handleSynthPage(p600Button_t button)
{
	const struct uiParam_s * prev;
	
	prev=ui.activeParam;
	
	if(button>=pb1 && button<=pb9)
	{
		ui.activeParam=&uiParameters[button-pb1];
		ui.previousData=-1;
	}
	else if(button==pb0)
	{
		ui.activeParam=&uiParameters[(potmux_getValue(ppSpeed)>>13)+9];
		ui.previousData=-1;
	}
	
	if(ui.activeParam!=prev)
	{
		sevenSeg_scrollText(ui.activeParam->name,1);
	}
}

void ui_dataPotChanged(void)
{
	ui.lastActivePot=potmux_lastChanged();
		
	if(ui.lastActivePot!=ppSpeed)
		return;
	
	// handle our "data" pot
	
	if(scanner_buttonState(pb0))
	{
		ui.lastActivePot=ppNone;
			
		handleSynthPage(pb0);
	}
	else if(ui.activeParam!=NULL)
	{
		int32_t data,valCount;
		
		data=potmux_getValue(ppSpeed);
		
		switch(ui.activeParam->type)
		{
		case ptCont:
			currentPreset.continuousParameters[ui.activeParam->number]=data;
			break;
		case ptStep:
		case ptCust:
			ui.lastActivePot=ppNone;
			
			valCount=0;
			while(valCount<4 && ui.activeParam->values[valCount]!=NULL)
				++valCount;
			
			data=(data*valCount)>>16;

			if(data!=ui.previousData)
				sevenSeg_scrollText(ui.activeParam->values[data],1);

			if(ui.activeParam->type==ptStep)
			{
				currentPreset.steppedParameters[ui.activeParam->number]=data;
			}
			else
			{
				int8_t br[]={3,5,12};
	
				switch(ui.activeParam->number)
				{
				case 0: // lfo shape 
					currentPreset.steppedParameters[spLFOShape]=(currentPreset.steppedParameters[spLFOShape]&1) | (data<<1);
					break;
				case 1: // lfo tgt
					if(data==1)
						currentPreset.steppedParameters[spLFOTargets]|=mtOnlyA;
					else if(data==2)
						currentPreset.steppedParameters[spLFOTargets]|=mtOnlyB;
					else
						currentPreset.steppedParameters[spLFOTargets]&=~(mtOnlyA|mtOnlyB);
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
				}
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
		if(ui.digitInput!=diSynth)
		{
			ui.digitInput=diSynth;
		}
		else
		{
			ui.digitInput=diLoadDecadeDigit;
		}
	}

	// preset mode
	
	if(pressed && button==pbPreset)
	{
		// save manual preset
		if (settings.presetBank==pbkManual)
			manualPreset=currentPreset;
		
		settings.presetBank=(settings.presetBank+1)%2; //TODO: second preset bank, how to store?
		settings_save();
		refreshPresetMode();
	}
	
	if(pressed && button==pbRecord)
	{
		if(ui.digitInput==diStoreDecadeDigit)
		{
			// cancel record
			ui.digitInput=(settings.presetBank==pbkManual)?diSynth:diLoadDecadeDigit;
		}
		else
		{
			// ask for digit
			ui.digitInput=diStoreDecadeDigit;
		}
	}
	
	if(ui.digitInput>=diLoadDecadeDigit)
	{
		// preset number input 
		
		if(pressed && button>=pb0 && button<=pb9)
		{
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
					ui.lastActivePot=ppNone;
					ui.presetModified=0;
					settings_save();		
				}

				ui.presetAwaitingNumber=-1;
				ui.digitInput=(settings.presetBank==pbkManual)?diSynth:diLoadDecadeDigit;
				break;
			default:
				;
			}
		}
	}
	else
	{
		if(scanner_buttonState(pbFromTape))
			handleMiscAction(button);
		else if(ui.digitInput==diSynth)
			handleSynthPage(button);
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
	ui.presetModified=1;
}