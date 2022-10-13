////////////////////////////////////////////////////////////////////////////////
// User interface
////////////////////////////////////////////////////////////////////////////////

#include "ui.h"
#include "storage.h"
#include "seq.h"
#include "arp.h"
#include "scanner.h"
#include "display.h"
#include "potmux.h"
#include "midi.h"
#include "stdio.h"

const struct uiParam_s uiParameters[] =
{
	/*first press*/
	/*0*/ {.type=ptCont,.number=cpSeqArpClock,.name="spd"},
	/*1*/ {.type=ptCust,.number=0,.name="lfo shp",.values={"tri-puls","sin-rnd","saw-nois"}},
	/*2*/ {.type=ptCont,.number=cpVibFreq,.name="Vib spd"},
	/*3*/ {.type=ptStep,.number=spModwheelTarget,.name="mod tgt",.values={"lfo","Vib"}},
	/*4*/ {.type=ptStep,.number=spChromaticPitch,.name="pitch",.values={"free","semi","oct", "oct-semi", "semi-oct", "oct-free", "free-oct"}},
	/*5*/ {.type=ptCust,.number=3,.name="fil shp",.values={"lin-slo","exp-slo","lin-fast","exp-fast"}},
    /*6*/ {.type=ptStep,.number=spBenderTarget,.name="bend tgt",.values={"off","ab","Vcf","Vol","b"}},
    /*7*/ {.type=ptStep,.number=spAssignerPriority,.name="prio",.values={"last","low","high"}},
	/*8*/ {.type=ptCont,.number=cpUnisonDetune,.name="detune"},
	/*9*/ {.type=ptCont,.number=cpAmpVelocity,.name="amp Vel"},
	/*second press*/
	/*0*/ {.type=ptCont,.number=cpSeqArpClock,.name="spd"},
	/*1*/ {.type=ptCust,.number=1,.name="lfo tgt",.values={"ab","a","b","ab-Vca"}},
	/*2*/ {.type=ptCont,.number=cpVibAmt,.name="Vib amt"},
    /*3*/ {.type=ptCont,.number=cpModDelay,.name="mod dly"},
	/*4*/ {.type=ptCont,.number=cpExternal,.name="ext volt"},
	/*5*/ {.type=ptCust,.number=2,.name="2nd shp",.values={"lin-slo","exp-slo","lin-fast","exp-fast"}},
    /*6*/ {.type=ptCust,.number=4,.name="bend rng",.values={"2nd","3rd","5th","Oct"}},
    /*7*/ {.type=ptStep,.number=spAssign,.name="assign",.values={"first","cycle"}},
    /*8*/ {.type=ptCont,.number=cpSpread,.name="vintage"},
	/*9*/ {.type=ptCont,.number=cpFilVelocity,.name="fil Vel"},
	/*third press*/
	/*0*/ {.type=ptCont,.number=0,.name="dummy"},
	/*1*/ {.type=ptStep,.number=spLFOSync,.name="lfo trig",.values={"off","key","1","2","3","4","5","6","8"}},
    /*2*/ {.type=ptStep,.number=spVibTarget,.name="Vib tgt",.values={"VCO","VCA","VCO A","VCO B"}},
	/*3*/ {.type=ptStep,.number=spModWheelRange,.name="mod rng",.values={"touch","soft", "high", "full"}},
	/*4*/ {.type=ptStep,.number=spPWMBug,.name="pulse bug",.values={"off","on"}},
	/*5*/ {.type=ptStep,.number=spEnvRouting,.name="route",.values={"std","poly-amp","poly","gate"}},
	/*6*/ {.type=ptCont,.number=0,.name="dummy"},
    /*7*/ {.type=ptCont,.number=cpGlide,.name="glide"},
    /*8*/ {.type=ptCont,.number=cpDrive,.name="drive"},
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
                (currentPreset.steppedParameters[spLFOTargets]&(mtOnlyA|mtOnlyB|mtVCA)) | // keep those as-is
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

// Return 1 if an action has been completed which does not require any
// further printouts (e.g. init patch).
static int8_t changeMiscSetting(p600Button_t button)
{
	switch(button)
	{
        case pb1: // midi receive channel
            settings.midiReceiveChannel=((settings.midiReceiveChannel+2)%17)-1;
            settings_save();
            return 0;
        case pb2: // midi send channel
            settings.midiSendChannel=(settings.midiSendChannel+1)%16;
            settings_save();
            return 0;
        case pb3: // pitch wheel calibration
            settings.benderMiddle=potmux_getValue(ppPitchWheel);
            settings_save();
            synth_updateBender(); // immediate update
            sevenSeg_scrollText("bender calibrated",1);
            return 1;
        case pb4: // voice selection
            ui.voice=(ui.voice+1)%SYNTH_VOICE_COUNT;
            return 0;
        case pb5: // selected voice defeat
            settings.voiceMask^=(1<<ui.voice);
            settings_save();
            refreshFullState();
            return 0;
        case pb6: //
            settings.panelLayout=((settings.panelLayout+1)%2);
            mixer_updatePanelLayout(settings.panelLayout);
            // reset the pick-up status of the two pots
            currentPreset.contParamPotStatus[cpMixVolA]=0;
            currentPreset.contParamPotStatus[cpGlideVolB]=0;
            settings_save();
            return 0;
        case pb7:
            return 1;
        case pb8: // sync mode
            settings.syncMode=(settings.syncMode+1)%3;
            settings_save();
            refreshFullState();
            return 0;
        case pb9: // spread / vcf limit
            settings.vcfLimit=(settings.vcfLimit?0:1); // only on or off
            settings_save();
            refreshFullState();
            return 0;
        case pb0: // MIDI mode, e.g. local on/off
            settings.midiMode=((settings.midiMode+1)%2);
            settings_save();
            if (settings.midiMode==1)
                synth_resetForLocalOffMode();
            return 0;
        case pbPreset: // reset to a basic patch
            preset_loadDefault(1);
            ui.presetModified=1;
            sevenSeg_scrollText("basic patch",1);
            // go directly into preset panel mode
            settings.presetMode=1;
            ui.digitInput=diSynth;
            ui.lastActivePot=ppNone;
            refreshFullState();
            return 1;
        default:
		break;
	}
	return 0;
}

static LOWERCODESIZE void handleMiscAction(p600Button_t button)
{
	const char * chs[17]={"omni","ch1","ch2","ch3","ch4","ch5","ch6","ch7","ch8","ch9","ch10","ch11","ch12","ch13","ch14","ch15","ch16"};
	char s[50];
	int8_t nothingToDisplay=0;

	if (button==ui.prevMiscButton ||
		button==pb4 || // pb4 is for voice selection, this one should be immediate
		(button==pb5 && ui.prevMiscButton==pb4)) // pb5 voice defeat after pb4 should be immediate too
		nothingToDisplay=changeMiscSetting(button);
	ui.prevMiscButton=button;
	if (nothingToDisplay)
		return;
	
	switch(button)
	{
	case pb1: // midi receive channel
		strcpy(s,chs[settings.midiReceiveChannel+1]);
		strcat(s," recv");
		sevenSeg_scrollText(s,1);
		break;
	case pb2: // midi send channel
		strcpy(s,chs[settings.midiSendChannel+1]);
		strcat(s," send");
		
		sevenSeg_scrollText(s,1);
		break;
	case pb3: // pitch wheel calibration
		sevenSeg_scrollText("again calibrates bender",1);
        synth_wheelEvent(0,0,1,0,0); // kill external MIDI pitch bend
		break;
	case pb4: // voice selection
	case pb5: // selected voice defeat
		if(settings.voiceMask&(1<<ui.voice))
			strcpy(s,"Vc- on");
		else
			strcpy(s,"Vc- off");

		s[2]='1'+ui.voice;
		sevenSeg_scrollText(s,1);
		break;
	case pb6:
        if (settings.panelLayout==0)
            strcpy(s,"GliGli Layout");
        else
            strcpy(s,"SCI Layout");

		sevenSeg_scrollText(s,1);
		break;
	case pb7: // that button doesn't work, simultaneous press with FromTape not possible
		break;
	case pb8: // sync mode
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
		break;
	case pb9: // vcf limit
		strcpy(s,"Vcf lim ");
		if(settings.vcfLimit)
		{
			strcat(s,"on");
		}
		else
		{
			strcat(s,"off");
		};

		sevenSeg_scrollText(s,1);
		break;
	case pb0: // local on/off
		if (settings.midiMode==0)
		{
			sevenSeg_scrollText("Local on",1);
		}
		else
		{
			sevenSeg_scrollText("Local off",1);
		}
		break;
	case pbPreset: // reset to a basic patch
		sevenSeg_scrollText("again sets basic patch",1);
		break;
	default:
		break;
	}
}

void ui_setLocalMode(uint8_t on)
{
    if (on==0)
    {
        settings.midiMode=0;

    }
    else
    {
        settings.midiMode=1;
    }
}

static LOWERCODESIZE void setCustomParameter(int8_t num, int32_t data)
{
	int8_t br[]={2,4,7,12};

	switch(num)
	{
	case 0: // lfo shape 
		currentPreset.steppedParameters[spLFOShape]=(currentPreset.steppedParameters[spLFOShape]&1) | (data<<1);
		break;
	case 1: // lfo tgt
		currentPreset.steppedParameters[spLFOTargets]&=~(mtOnlyA|mtOnlyB|mtVCA);
		if(data==1)
			currentPreset.steppedParameters[spLFOTargets]|=mtOnlyA;
		else if(data==2)
			currentPreset.steppedParameters[spLFOTargets]|=mtOnlyB;
		else if(data==3)
			currentPreset.steppedParameters[spLFOTargets]|=mtVCA;
		break;					
	case 2: // amp shape
        currentPreset.steppedParameters[spAmpEnvShape]=(data&1);
		currentPreset.steppedParameters[spAmpEnvSlow]=1-((data&2)>>1);
		break;
	case 3: // fil shape
        currentPreset.steppedParameters[spFilEnvShape]=(data&1);
		currentPreset.steppedParameters[spFilEnvSlow]=1-((data&2)>>1);
		break;
	case 4: // bend range 
		currentPreset.steppedParameters[spBenderSemitones]=br[data];
		synth_updateBender(); // immediate update
		break;
	}
}

static LOWERCODESIZE void displayUIParameter(int8_t num)
{
	int8_t i;
	char s[20];
	const struct uiParam_s * prm = &uiParameters[ui.activeParamIdx];

	strcpy(s,prm->name);
	strcat(s,"= ");
	
	switch(prm->type)
	{
        case ptCont:
            s[strlen(s)-1]='\0'; // remove trailing space
            ui.lastActivePotValue=ui.adjustedLastActivePotValue=currentPreset.continuousParameters[prm->number]; // this is only for menu parameters, not pots
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
			{if (new==pb1||new==pb2||new==pb3||new==pb4||new==pb5||(new==pb8 && settings.panelLayout==1) || (new==pb7 && settings.panelLayout==0)) // parameters on third press, include Drive (8) only in SCI panel layout, include Glide (7) only in Gligli layout
				{ui.activeParamIdx+=10;}
			else
				ui.activeParamIdx-=10;}
		else if (prev==new+20)
			ui.activeParamIdx-=20;
		else
			ui.activeParamIdx=new;

        ui.previousData=-1;
	}

    //potmux_resetSpeedPot();
    ui_setNoActivePot(1);

    if(ui.activeParamIdx!=prev || ui.lastActivePot!=ppNone) // should be displayed either if
	{
		// display param name + value
		displayUIParameter(ui.activeParamIdx);
		
		// set flag for enabling storage of manual preset if that new parameter is changed
        ui.menuParamSelectChange=1;
	}


}

static LOWERCODESIZE void handleSequencerPage(p600Button_t button)
{
	// sequencer note input		
	if(seq_getMode(0)==smRecording || seq_getMode(1)==smRecording)
	{
		uint8_t note;
		switch(button)
		{
		case pb0:
			note=SEQ_NOTE_CLEAR;
			break;
		case pb1:
			note=SEQ_NOTE_UNDO;
			break;
		case pb2:
			note=SEQ_NOTE_STEP;
			break;
		default:
			note=ASSIGNER_NO_NOTE;
		}
		
		if(note!=ASSIGNER_NO_NOTE)
			seq_inputNote(note,1);
	}
}

void ui_setNoActivePot(uint8_t fullPotmuxReset)
{
	if (fullPotmuxReset)
    {
        potmux_resetChangedFull();
    }
    else
    {
        potmux_resetChanged();
    }
	ui.lastActivePot=ppNone;
	ui.lastActivePotValue=-1;

}

FORCEINLINE void ui_setPresetModified(int8_t modified)
{
	ui.presetModified=modified;
}

void ui_checkIfDataPotChanged(void)
{
	ui.lastActivePot = potmux_lastChanged() != ppNone ? potmux_lastChanged() : ui.lastActivePot;
	
	if(ui.lastActivePot!=ppSpeed || ui.isInPatchManagement)
		return;
	
    int32_t data,valCount;
    data=potmux_getValue(ppSpeed);

    if (ui.isShifted && settings.presetMode && (ui.digitInput==diLoadUnitDigit || ui.digitInput==diLoadDecadeDigit)) // this is preset load patch - we want the data dial to work as selector
    {
        // map dial onto 0...99
        // if in local off mode we can still change the program because the incoming MIDI would have no effect
        uint16_t selectedPatch;
        //char s[50];
        selectedPatch=(data*100)>>16; // this divides the total range (16 bits) into 0...99 range using effectively a floor() function

        if (selectedPatch==settings.presetNumber) return;

        if(preset_loadCurrent(selectedPatch,0))
        {
            midi_sendProgChange(settings.presetNumber); // only send when new prog is selected
            refreshFullState();
            ui.presetModified=0;
        }
        settings.presetNumber=selectedPatch;
        // override the already selected unit input
        ui.digitInput=diLoadDecadeDigit;
        ui.presetAwaitingNumber=-1;
        return;
    }

    // handle our "data" pot
    if(ui.activeParamIdx>=0) 	// normal parameter selection
	{
		struct uiParam_s prm;
		
		if(data==ui.lastActivePotValue) // prevent slowdown caused by unwanted updates
			return;
		
		prm=uiParameters[ui.activeParamIdx];
		
		switch(prm.type)
		{
            case ptCont:
                if (prm.number==cpSeqArpClock) // special treatment of the seq/arp speed --> part of settings, but display uses preset params, so update both
                    settings.seqArpClock=data;
                if (prm.number==cpVibAmt) ui.vibAmountChangePending=1;
                if (prm.number==cpVibFreq) ui.vibFreqChangePending=1;
                currentPreset.continuousParameters[prm.number]=data;
                break;
            case ptStep:
            case ptCust:
                ui_setNoActivePot(0);

                valCount=0;
                while(valCount<8 && prm.values[valCount]!=NULL) // 8 is the current max of choices
                    ++valCount;

                data=(data*valCount)>>16; // this divides the total range (16 bits) into valCount pieces using effectively a floor() function

                if(data!=ui.previousData && ui.digitInput==diSynth)
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

        if (ui.menuParamSelectChange==1) // this make sure the the MANUAL_PRESET_PAGE is only stored the first time a NEW menu parameter is CHANGED
        {
            if(!settings.presetMode)
                preset_saveCurrent(MANUAL_PRESET_PAGE);
        }
        ui.menuParamSelectChange=0; // ensures that no storage is possible until a new menu parameter is selected
        if (prm.number==spModWheelRange || prm.number==spModwheelTarget) synth_wheelEvent(0,potmux_getValue(ppModWheel),2,1,0); // take effect immediately
        if (prm.number!=cpSeqArpClock) ui.presetModified=1;
		
		refreshFullState();
	}
}

void LOWERCODESIZE ui_handleButton(p600Button_t button, int pressed)
{
	int8_t recordOverride=0;
    char s[15];

	// button press might change current preset

	refreshPresetButton(button);

	// sequencer
	
	if(pressed && (button==pbSeq1 || button==pbSeq2) && !ui.isInPatchManagement)
	{
		int8_t track=(button==pbSeq2)?1:0;
		
		switch(seq_getMode(track))
		{
            case smOff:
                if(ui.digitInput==diStoreDecadeDigit || ui.digitInput==diStoreUnitDigit)
                {
                    // go directly to record mode when record was previously pressed
                    seq_setMode(track,smRecording);
                    // seq mode
                    ui.digitInput=diSequencer;
                }
                else
                {
                    seq_setMode(track,ui.isShifted?smWaiting:smPlaying);
                }
                break;
            case smRecording:
                // cancel seq mode
                ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth;
                // fall through to start playing
            case smWaiting:
                seq_setMode(track,smPlaying);
                break;
            case smPlaying:
                seq_setMode(track,smOff);
                break;
		}
	}
	
	// arp
	
	if (pressed && !ui.isInPatchManagement && seq_getMode(0)!=smRecording && seq_getMode(1)!=smRecording)
    {
        if(button==pbArpUD)
        {
            if (arp_getMode()==amUpDown)
            {
                arp_setMode(amOff,arp_getHold());
            }
            else
            {
                ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth; // cancel special mode (if any)
                arp_setMode(amUpDown,arp_getHold());
            }
        }
        else if(button==pbArpAssign)
        {
            switch(arp_getMode())
            {
                case amOff:
                    ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth; // cancel special mode (if any)
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
    }
	
	if(arp_getMode()!=amOff && pressed && button==pbRecord && !ui.isInPatchManagement)
	{
		arp_setMode(arp_getMode(),arp_getHold()?0:1);
		recordOverride=1; // override normal record action for the rest of the function, already used up for toggling arp hold
	}

	// assigner
	
	if(button==pbUnison)
	{
		if(pressed)
		{
			assigner_latchPattern(0);
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
	
	if(pressed && button==pbToTape && settings.presetMode && !ui.isInPatchManagement)
	{
		if(ui.digitInput!=diSynth) // preset select or store mode, sequencer mode
		{
			ui.digitInput=diSynth; // parameter select mode
		}
		else // parameter select mode
		{
			if (seq_getMode(0)==smRecording || seq_getMode(1)==smRecording)
			{
				ui.digitInput=diSequencer;
			}
			else
			{
				ui.digitInput=diLoadDecadeDigit; // mode wait for first digit of preset selection
			}
		}
		ui_setNoActivePot(1); // no active pot status should be carried over to the new state
	}

	// shifted state (keyboard transposition, ...)
	
	if(button==pbFromTape)
	{
		if (pressed)
		{
			// If we're in double click mode, exit it when
			// button pressed once. Otherwise enter double click
			// mode if button pressed < 1 second ago, else it
			// counts as first time, so set up double click timer.
			if (ui.isDoubleClicked)
				ui.isDoubleClicked=0;
			else if (ui.doubleClickTimer)
			// Button pressed < 1 second ago
			{
				ui.doubleClickTimer=0; // reset timer
				ui.isDoubleClicked=1;
				assigner_allKeysOff(); // make sure that voice are finished, as key events will be used for transposition
			}
			else
            {
				ui.doubleClickTimer = 63; // 1 second
            }
		}
		ui.isShifted=pressed;
        // when shifted or unshifted the speed pot changes its function, so make sure the pot value is not applied
        if (ui.lastActivePot==ppSpeed) ui.lastActivePot=ppNone;
        potmux_resetSpeedPot();
		// reset Misc Settings to 'display only' whenever
		// pbFromTape is pressed (or released).
		ui.prevMiscButton=-1;
	}
	
	// modes 
	
	if(pressed)
	{
		if (ui.isInPatchManagement && button==pbRecord)
        {
            ui.isInPatchManagement=0;
            ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth;
        }
        else if (ui.isInPatchManagement && button==pbPreset)
        {
            // dump the patch bank
            midi_dumpPresets();
            sevenSeg_scrollText("presets dumped",1);
            refreshPresetMode();
            ui.digitInput=diStoreDecadeDigit;
        }
        else if ((ui.isShifted || ui.isDoubleClicked) && ((button>=pb0 && button<=pb9) || button==pbTune || button==pbPreset || button==pbRecord))
		// these are the special function buttons in shift mode
		{
			// Disable double click mode which might confuse
			// user if she/he presses FROM TAPE within the double
			// click interval while fiddling with the misc params.
			ui.doubleClickTimer=0; // reset timer
			// ui.isDoubleClicked=0; // maybe not?? Let's try without

			if (button==pbTune)
			{
				ui.retuneLastNotePressedMode=!ui.retuneLastNotePressedMode;
			}
			else if (button==pbRecord && !recordOverride) // when arp is running then patch mgmt mode cannot be entered.
			{
				ui.isInPatchManagement=1;
                ui.digitInput=diStoreDecadeDigit; // the mode should start with expecting a number for single patch export
                ui.presetAwaitingNumber=-1;
                ui.isDoubleClicked=0; // switch this off to avoid confusion with the function of the number pad
                sevenSeg_setAscii(' ',' ');
			}
			else if (!ui.isInPatchManagement) // otherwise this would potentially go into the shifted function of those keys
            {
                handleMiscAction(button);
            }
		}
		else if(button==pbTune)
		{
			if (!ui.retuneLastNotePressedMode)
			{
				synth_tuneSynth();
			}
			else
			{
				ui.retuneLastNotePressedMode=0;
			}
		}
		else if(button==pbPreset) // toggle between live mode and preset mode
		{
            // save manual preset to recall it later
            if(!settings.presetMode) // switch to preset mode
            {
                preset_saveCurrent(MANUAL_PRESET_PAGE);
            }

            settings.presetMode=settings.presetMode?0:1;
            settings_save();
            refreshPresetMode();
            refreshFullState();
		}
		else if(button==pbRecord && !recordOverride) // patch storage mode on/off
        {
                if(ui.digitInput==diStoreDecadeDigit || ui.digitInput==diStoreUnitDigit) // storage mode switched off
                {
                    ui.digitInput=(settings.presetMode)?diLoadDecadeDigit:diSynth;
                    ui.presetAwaitingNumber=-1;
                }
                else // storage mode switch on
                {
                    // ask for digit
                    ui.digitInput=diStoreDecadeDigit;
                }
		}
		else if(ui.digitInput==diSequencer)
        {
            handleSequencerPage(button);
        }
        else if(ui.digitInput==diSynth)
        {
            // parameter selection and display shows last touched control value
            handleSynthPage(button);
        }
		else if(ui.digitInput>=diLoadDecadeDigit && button>=pb0 && button<=pb9)
		{
			// preset number input for load an store
			switch(ui.digitInput)
			{
                case diLoadDecadeDigit: // this is the first press of the preset select
                    ui.presetAwaitingNumber=button-pb0;
                    ui.digitInput=diLoadUnitDigit;
                    break;
                case diStoreDecadeDigit: // this is the first press of the prese store
                    ui.presetAwaitingNumber=button-pb0;
                    ui.digitInput=diStoreUnitDigit;
                    break;
                case diLoadUnitDigit: // this is the first press of the preset select
                case diStoreUnitDigit: // this is the second press of the preset store
                    ui.presetAwaitingNumber=ui.presetAwaitingNumber*10+(button-pb0);

                    if(!ui.isInPatchManagement)
                    {
                        // store?
                        if(ui.digitInput==diStoreUnitDigit)
                        {
                            if (!settings.presetMode) preset_saveCurrent(MANUAL_PRESET_PAGE); // make sure that the latest parameters are stored for live mode
                            preset_saveCurrent(ui.presetAwaitingNumber);
                        }
                        // if in local off mode we can still change the program because the incoming MIDI would have no effect
                        // also: always try to load/reload preset
                        if(preset_loadCurrent(ui.presetAwaitingNumber,0))
                        {
                            settings.presetNumber=ui.presetAwaitingNumber;
                            if(ui.digitInput!=diStoreUnitDigit) // only send MIDI and store selection when new prog is selected, not when it is reloaded after storage
                            {
                                midi_sendProgChange(settings.presetNumber);
                                settings_save();
                            }
                        }

                        refreshPresetMode();
                        refreshFullState();
                    }
                    else
                    {
                        sevenSeg_setAscii(' ',' ');
                        ui.digitInput=diStoreDecadeDigit;
                        if (midi_dumpPreset(ui.presetAwaitingNumber)) // dump that patch
                        {
                            sprintf(s, "patch %u dumped", ui.presetAwaitingNumber);
                            sevenSeg_scrollText(s,1);
                        }
                    }

                    ui.presetAwaitingNumber=-1;

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
		
	ui.presetAwaitingNumber=-1;
	ui.lastActivePotValue=-1;
	ui.presetModified=0;
    settings.presetMode=1; // start in preset mode
	ui.digitInput=diSynth; // panel mode
	ui.activeParamIdx=0; // select clock/speed
	ui.prevMiscButton=-1;
    ui.menuParamSelectChange=0;
}

// Called at 63Hz
void ui_update(void)
{
	if (ui.doubleClickTimer)
		ui.doubleClickTimer--;
}
