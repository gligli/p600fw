#ifndef STORAGE_H
#define	STORAGE_H

#include "synth.h"
#include "tuner.h"
#include "assigner.h"

#define MANUAL_PRESET_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-5)
// V2.5 (Teensy): 24 pages below the top of the storage area, which is the flash address the V2.34 sequencer used (page 200 of 224 there,
// page 104 of 128 here), so a sysex update from V2.34 keeps the sequences (the format is the same). It must stay above the presets 0..99.
#define SEQUENCER_START_PAGE ((STORAGE_SIZE/STORAGE_PAGE_SIZE)-24)

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
    spArpEuclid=30, // arpeggiator Euclidean rhythm, 0 = off (every step plays), see arp.c. Added in storage version 9
    spArpProbability=31, // arpeggiator: chance that a step plays, 0 = always (100%), see arp.c. Added in storage version 10
    spArpSwing=32, // arpeggiator: delay of every second step, 0 = off (straight), see arp.c. Added in storage version 10
    spArpResolution=33, // arpeggiator: step length relative to the clock step, 0 = 1/16 (the clock step itself), see arp.c. Added in storage version 11
    spArpRatchet=34, // arpeggiator: number of times a step is played within its length, 0 = off (once), see arp.c. Added in storage version 12
    spArpRatchetProb=35, // arpeggiator: chance that a step is ratcheted, 0 = always (100%), see arp.c. Added in storage version 12
    spEnsemble=36, // ensemble vibrato: 0 = off, 1 = small, 2 = wide spread of the vibrato phase between the unison voices, see synth.c. Added in storage version 13
    spPolyModEnv=37, // target of the poly-mod envelope (the Freq A poly-mod switch and the poly-mod envelope amount): 0 = VCO A (the original behaviour, so old presets are unchanged), 1 = VCO B, 2 = both VCOs, 3 = pulse width A, 4 = pulse width B, 5 = both pulse widths, see synth.c. Added in storage version 15

    spVibShape=38, // shape of the vibrato LFO, one of the lfoShape_t shapes (0..LFO_SHAPE_COUNT-1). The default is the triangle, which is what the vibrato always was. Added in storage version 16
    spVibRange=39, // speed range of the vibrato: 0 = normal (the speed the vibrato always had), 1 = low (8 times slower). Added in storage version 16

	// /!\ this must stay last
	spCount
} steppedParameter_t;

// the vibrato shape of a preset that does not store one (version 15 and older) is the triangle (lsTri in lfo.h)
#define VIB_SHAPE_DEFAULT 1

// number of vibrato targets (see spVibTarget): VCO, VCA, VCO A, VCO B, and new: noise level, resonance, detune (spread of the voices), poly-mod amount (Osc B)
#define VIB_TARGETS 8

// number of poly-mod envelope targets (including 0 = VCO A), see spPolyModEnv
#define POLYMOD_ENV_TARGETS 6

// number of selectable arpeggiator Euclidean rhythms (including 0 = off), see arp.c
#define ARP_EUCLID_PATTERNS 8
// number of selectable arpeggiator step probabilities and swing amounts (including 0 = off), see arp.c
#define ARP_PROBABILITY_CHOICES 8
#define ARP_SWING_CHOICES 8
// number of selectable arpeggiator resolutions: 1/16, 1/16 triplet, 1/32, 1/32 triplet, see arp.c
#define ARP_RESOLUTION_CHOICES 4
// number of selectable arpeggiator ratchets (off, 2, 3, 4 hits per step) and ratchet probabilities, see arp.c
#define ARP_RATCHET_CHOICES 4
#define ARP_RATCHET_PROB_CHOICES 8

// number of ensemble vibrato settings (off, small, wide), see synth.c
#define ENSEMBLE_CHOICES 3

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
    uint8_t bootLightShow; // 1 = play the LED light show at power-up (default), 0 = off; switched in the From Tape menu with keypad 7 (new in storage version 14)

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
void settings_reset(void);			//Flat

void storage_simpleExport(uint16_t number, uint8_t * buf, int16_t size);
void storage_export(uint16_t number, uint8_t * buf, int16_t * loadedSize);
void storage_import(uint16_t number, uint8_t * buf, int16_t size);

int8_t storage_loadSequencer(int8_t track, uint8_t * data, uint8_t size);
void storage_saveSequencer(int8_t track, uint8_t * data, uint8_t size);

#endif	/* STORAGE_H */

