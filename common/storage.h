#ifndef STORAGE_H
#define	STORAGE_H

#include "p600.h"
#include "tuner.h"

struct settings_s
{
	uint16_t tunes[TUNER_OCTAVE_COUNT][TUNER_CV_COUNT];
	uint16_t currentPresetNumber;
};

struct preset_s
{
};

extern struct settings_s settings;
extern struct preset_s currentPreset;

int8_t settings_load(void);
void settings_save(void);

int8_t preset_loadCurrent(uint16_t number);
void preset_saveCurrent(uint16_t number);

#endif	/* STORAGE_H */

