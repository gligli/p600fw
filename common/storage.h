#ifndef STORAGE_H
#define	STORAGE_H

#include "p600.h"
#include "tuner.h"
#include "assigner.h"

typedef enum
{
	cpFreqA=0,cpVolA=1,cpAPW=2,
	cpFreqB=3,cpVolB=4,cpBPW=5,cpFreqBFine=6,
	cpCutoff=7,cpResonance=8,cpFilEnvAmt=9,
	cpFilRel=10,cpFilSus=11,cpFilDec=12,cpFilAtt=13,
	cpAmpRel=14,cpAmpSus=15,cpAmpDec=16,cpAmpAtt=17,
	cpPModFilEnv=18,cpPModOscB=19,
	cpLFOFreq=20,cpLFOAmt=21,
	cpGlide=22,
	cpAmpVelocity=23,cpFilVelocity=24,

	// /!\ this must stay last
	cpCount
} continuousParameter_t;

typedef enum
{
	spASaw=0,spATri=1,spASqr=2,
	spBSaw=3,spBTri=4,spBSqr=5,
			
	spSync=6,spPModFA=7,spPModFil=8,
			
	spLFOShape=9,spLFOShift=10,spLFOTargets=11,

	spTrackingShift=12,
	spFilEnvExpo=13,spFilEnvSlow=14,
	spAmpEnvExpo=15,spAmpEnvSlow=16,
			
	spUnison=17,
	spAssignerMonoMode=18,
			
	spBenderSemitones=19,spBenderTarget=20,
	spModwheelShift=21,

	spChromaticPitch=22,

	// /!\ this must stay last
	spCount
} steppedParameter_t;

struct settings_s
{
	uint16_t tunes[TUNER_OCTAVE_COUNT][TUNER_CV_COUNT];

	uint16_t benderMiddle;

	uint16_t presetNumber;
	enum {pbkManual=0,pbkA=1,pbkB=2} presetBank;
	
	int8_t midiReceiveChannel; // -1: omni / 0-15: channel 1-16
};

struct preset_s
{
	uint8_t steppedParameters[spCount];
	uint16_t continuousParameters[cpCount];
};

extern struct settings_s settings;
extern struct preset_s currentPreset;
extern const uint8_t steppedParametersBits[spCount];

int8_t settings_load(void);
void settings_save(void);

int8_t preset_loadCurrent(uint16_t number);
void preset_saveCurrent(uint16_t number);

#endif	/* STORAGE_H */

