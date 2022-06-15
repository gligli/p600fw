#ifndef STORAGE_H
#define	STORAGE_H

#include "synth.h"
#include "tuner.h"
#include "assigner.h"

#define MANUAL_PRESET_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-5)
#define SEQUENCER_START_PAGE 200

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
			
	cpModDelay=25,
	cpVibFreq=26,cpVibAmt=27,
	cpUnisonDetune=28,
	cpSeqArpClock=29,
    cpExternal=30,
    cpSpread=31,
    cpMixVolA=32,
    cpGlideVolB=33,
    cpDrive=34,

	// /!\ this must stay last
	cpCount
} continuousParameter_t;

// These start at 48, which means that MIDI hold pedal will end up at
// offset 16, so don't use that one for a parameter.
typedef enum
{
	spASaw=0,spATri=1,spASqr=2,
	spBSaw=3,spBTri=4,spBSqr=5,
			
	spSync=6,spPModFA=7,spPModFil=8,
			
	spLFOShape=9,
    spLFOTargets=11,

	spTrackingShift=12,
	spFilEnvShape=13,spFilEnvSlow=14,
	spAmpEnvShape=15,holdPedal=16, // attention: 16 is actually the AMP envelope slow flag
			
	spUnison=17,
	spAssignerPriority=18,
			
	spBenderSemitones=19,spBenderTarget=20,
	spModWheelRange=21,

	spChromaticPitch=22,
			
	spModwheelTarget=23,
	spVibTarget=24,
	spAmpEnvSlow=25,
	spPWMBug=26,
    spAssign=27,
	spEnvRouting=28,
    spLFOSync=29,

	// /!\ this must stay last
	spCount
} steppedParameter_t;

struct settings_s
{
	uint16_t tunes[TUNER_OCTAVE_COUNT][TUNER_CV_COUNT];

	uint16_t benderMiddle;

	uint16_t presetNumber;
	int8_t presetMode;
	
	int8_t midiReceiveChannel; // -1: omni / 0-15: channel 1-16
	int8_t midiSendChannel; // 0-15: channel 1-16
	uint8_t voiceMask;
	
	int8_t syncMode;
	
	int8_t vcfLimit;
	
	uint16_t seqArpClock;
	uint8_t midiMode;
    uint8_t panelLayout; // 0 = GliGli and 1 =  SCI

};

#define TUNING_UNITS_PER_SEMITONE 5461.3333333 // 1/5461 of a semitone (== pow(2,16)/12)

struct preset_s
{
	uint8_t steppedParameters[spCount];
	uint16_t continuousParameters[cpCount];

    // two status variables (pots and switches) used for pick-me-up mode
    uint8_t contParamPotStatus[cpCount]; // this indicates whether the pot position is smaller or larger then the current cp

    uint8_t patchName[16];
	
	uint8_t voicePattern[SYNTH_VOICE_COUNT];
  
  uint16_t perNoteTuning[TUNER_NOTE_COUNT]; // see TUNING_UNITS_PER_SEMITONE
};

extern struct settings_s settings;
extern struct preset_s currentPreset;
extern const uint8_t steppedParameterRange[spCount];

int8_t settings_load(void);
void settings_save(void);

int8_t preset_checkPage(uint16_t number);
int8_t preset_loadCurrent(uint16_t number, uint8_t loadFromBuffer);
void preset_saveCurrent(uint16_t number);

void preset_loadDefault(int8_t makeSound);
void settings_loadDefault(void);

void storage_simpleExport(uint16_t number, uint8_t * buf, int16_t size);
void storage_export(uint16_t number, uint8_t * buf, int16_t * loadedSize);
void storage_import(uint16_t number, uint8_t * buf, int16_t size);

int8_t storage_loadSequencer(int8_t track, uint8_t * data, uint8_t size);
void storage_saveSequencer(int8_t track, uint8_t * data, uint8_t size);

#endif	/* STORAGE_H */

