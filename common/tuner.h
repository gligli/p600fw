#ifndef TUNER_H
#define	TUNER_H

#include "p600.h"

uint16_t tuner_computeNoteCV(uint8_t midiNote,p600CV_t cv);
void tuner_tuneSynth(void);

#endif	/* TUNER_H */

