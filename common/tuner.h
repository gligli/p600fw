#ifndef TUNER_H
#define	TUNER_H

#include "p600.h"

#define TUNER_CV_COUNT (pcFil6-pcOsc1A+1)
#define TUNER_OCTAVE_COUNT 12

uint16_t tuner_computeCVFromNote(uint8_t note, uint8_t nextInterp, p600CV_t cv);

void tuner_init(void);
void tuner_tuneSynth(void);

#endif	/* TUNER_H */

