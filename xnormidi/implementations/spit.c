#include <inttypes.h>
#include "serial_midi.h"

#define MIDI_CLOCK_12MHZ_OSC 23
#define MIDI_CLOCK_RATE MIDI_CLOCK_12MHZ_OSC

int main(void){
	//init midi, give the clock rate setting, indicate that we want only output
	MidiDevice * midi_device = serial_midi_init(MIDI_CLOCK_RATE, true, false);

   uint8_t cnt = 0;
   
   while (1) {
      midi_send_cc(midi_device, 0, 0, cnt);
      cnt = (cnt + 1) & 0x7F;
   }
   return 0;
}
