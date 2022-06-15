#ifndef MIDI_H
#define	MIDI_H

#include "synth.h"

void midi_init(void);
void midi_update(int8_t onlySend);
void midi_newData(uint8_t data);
uint8_t midi_dumpPreset(int8_t number);
void midi_dumpPresets(void);
void midi_sendNoteEvent(uint8_t note, int8_t gate, uint16_t velocity);
void midi_sendWheelEvent(int16_t bend, uint16_t modulation, uint8_t mask);
void midi_sendSustainEvent(int8_t on);
void midi_sendProgChange(uint8_t prog);
void midi_sendThreeBytes(uint8_t mdchn, uint16_t val);

#endif	/* MIDI_H */

