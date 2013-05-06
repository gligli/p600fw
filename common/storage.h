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
	bpASaw=1,bpATri=2,bpASqr=4,bpSync=8,
	bpBSaw=16,bpBTri=32,bpBSqr=64,
	bpPModFA=128,bpPModFil=256,
	bpLFOShape=512,
	bpUnison=1024,
	bpChromaticPitch=2048,

	// /!\ this must stay last
	bpForce32=UINT32_MAX
} bitParameter_t;

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
	uint8_t envFlags[2]; // 0:amp / 1:fil
	
	int8_t trackingShift;

	assignerMode_t assignerMonoMode;
	
	int8_t lfoAltShapes;
	modulation_t lfoTargets;
	uint8_t lfoShift;
	
	int8_t modwheelShift;
	
	int8_t benderSemitones;
	modulation_t benderTarget;
	
	uint32_t bitParameters;
	uint16_t continuousParameters[cpCount];
};

extern struct settings_s settings;
extern struct preset_s currentPreset;

int8_t settings_load(void);
void settings_save(void);

int8_t preset_loadCurrent(uint16_t number);
void preset_saveCurrent(uint16_t number);

#endif	/* STORAGE_H */

