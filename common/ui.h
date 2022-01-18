#ifndef UI_H
#define	UI_H

#include "synth.h"

enum uiDigitInput_e {
	diSynth, // live mode
	diLoadDecadeDigit, // waiting for the first number for preset select 
	diStoreDecadeDigit, // waiting for the first number for preset store 
	diLoadUnitDigit, // waiting for the second number for preset select
	diStoreUnitDigit, // waiting for the second number for preset store
	diSequencer,}; // sequencer record mode
enum uiParamType_e {ptCont,ptStep,ptCust};

struct uiParam_s
{
	enum uiParamType_e type;
	int8_t number;
	const char * name;
	const char * values[8];
};

struct ui_s
{
	enum uiDigitInput_e digitInput;
	int8_t presetAwaitingNumber;
	int8_t presetModified;

	p600Pot_t lastActivePot;
    uint8_t activeSwitch;

	int32_t lastActivePotValue;
	uint16_t adjustedLastActivePotValue;
	
	int32_t previousData;
	int8_t activeParamIdx;
	int8_t isShifted;

	int8_t isDoubleClicked;
    int8_t isInPatchManagement;
	int8_t doubleClickTimer;
	p600Button_t prevMiscButton;
	int8_t voice;
	int8_t retuneLastNotePressedMode;
    uint8_t menuParamSelectChange;
};

extern struct ui_s ui;

void ui_init(void);
void ui_checkIfDataPotChanged(void);
void ui_handleButton(p600Button_t button, int pressed);
void ui_setPresetModified(int8_t modified);
void ui_setNoActivePot(void);
void ui_setLocalMode(uint8_t on);
void ui_update(void);
void setButtonState(p600Button_t button, uint8_t oldVal, uint8_t switchStat, uint8_t newVal);
void setButtonStateAndValue(p600Button_t button, uint8_t sp, uint8_t switchStat, uint8_t newVal);

#endif	/* UI_H */

