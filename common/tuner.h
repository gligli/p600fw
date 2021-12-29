#ifndef TUNER_H
#define	TUNER_H

#include "synth.h"

#define TUNER_CV_COUNT (pcFil6-pcOsc1A+1)
#define TUNER_OCTAVE_COUNT 8 // changing this will break settings storage!
#define TUNER_NOTE_COUNT 12 // currently we only store the 12-scale degrees
  
uint16_t tuner_computeCVFromNote(uint8_t note, uint8_t nextInterp, p600CV_t cv);
uint16_t tuner_computeCVPerOct(uint8_t note, p600CV_t cv);

void tuner_init(void);
void tuner_tuneSynth(void);
void tuner_scalingAdjustment(void);
void tuner_setNoteTuning(uint8_t note, double numSemitonesAboveFundamental);
#endif	/* TUNER_H */  
