#ifndef TUNER_H
#define	TUNER_H

#include "p600.h"

uint16_t tuner_computeCVFromNote(uint8_t note,p600CV_t cv);
uint16_t tuner_computeCVFromFrequency(float frequency,p600CV_t cv);
void tuner_tuneSynth(void);

#endif	/* TUNER_H */
