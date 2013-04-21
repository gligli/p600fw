#ifndef SERIAL_MIDI_H
#define SERIAL_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif 

#include <inttypes.h>
#include "midi.h"

//initialize serial midi and return the device pointer
MidiDevice* serial_midi_init(uint16_t clockScale, bool out, bool in);

#ifdef __cplusplus
}
#endif 

#endif
