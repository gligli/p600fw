#ifndef MIDI_H
#define	MIDI_H

#include "synth.h"

void midi_init(void);
void midi_update(void);
void midi_newData(uint8_t data);
void midi_dumpPresets(void);

#endif	/* MIDI_H */

