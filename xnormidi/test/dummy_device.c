//midi for embedded chips,
//Copyright 2010 Alex Norman
//
//This file is part of avr-midi.
//
//avr-midi is free software: you can redistribute it and/or modify
//it under the terms of the GNU General Public License as published by
//the Free Software Foundation, either version 3 of the License, or
//(at your option) any later version.
//
//avr-midi is distributed in the hope that it will be useful,
//but WITHOUT ANY WARRANTY; without even the implied warranty of
//MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//GNU General Public License for more details.
//
//You should have received a copy of the GNU General Public License
//along with avr-midi.  If not, see <http://www.gnu.org/licenses/>.

#include "midi_device.h"
#include "midi.h"
#include <stdio.h>
#include <assert.h>

MidiDevice test_device;

uint8_t sent[3];
uint8_t got[3];

unsigned int cc_called;
unsigned int noteon_called;
unsigned int noteoff_called;
unsigned int aftertouch_called;
unsigned int pitchbend_called;
unsigned int songposition_called;
unsigned int progchange_called;
unsigned int chanpressure_called;
unsigned int songselect_called;
unsigned int tc_quarterframe_called;
unsigned int realtime_called;
unsigned int tunerequest_called;
unsigned int fallthrough_called;
unsigned int catchall_called;
unsigned int sysex_called;

void send_func(MidiDevice * device, uint16_t cnt, uint8_t byte0, uint8_t byte1, uint8_t byte2) {
   //just in case
   cnt = cnt % 4;

   sent[0] = byte0;
   sent[1] = byte1;
   sent[2] = byte0;
   printf("sent: ");
   uint8_t i;
   for (i = 0; i < cnt; i++) {
      printf("%02x ", sent[i]);
   }
   printf("\n");
}


void reset() {
   uint8_t i;
   for(i = 0; i < 3; i++)
      got[i] = sent[i] = 0;
   cc_called = 0;
   noteon_called = 0;
   noteoff_called = 0;
   aftertouch_called = 0;
   pitchbend_called = 0;
   songposition_called = 0;
   progchange_called = 0;
   chanpressure_called = 0;
   songselect_called = 0;
   tc_quarterframe_called = 0;
   realtime_called = 0;
   tunerequest_called = 0;
   fallthrough_called = 0;
   catchall_called = 0;
   sysex_called = 0;
}

bool anything_called() {
   return cc_called ||
      noteon_called ||
      noteoff_called ||
      aftertouch_called ||
      pitchbend_called ||
      songposition_called ||
      progchange_called ||
      chanpressure_called ||
      songselect_called ||
      tc_quarterframe_called ||
      realtime_called ||
      tunerequest_called ||
      fallthrough_called ||
      catchall_called ||
      sysex_called;
}

int main(void) {
   midi_device_init(&test_device);
   midi_device_set_send_func(&test_device, send_func);

   midi_send_cc(&test_device, 0, 0, 1);
   midi_send_cc(&test_device, 15, 1, 1);

   reset();
   midi_register_fallthrough_callback(&test_device, fallthrough_callback);
   assert(!anything_called());
   midi_device_input(&test_device, 3, 0xB0, 0, 1);
   midi_device_input(&test_device, 1, MIDI_CLOCK, 0, 0);
   midi_device_process(&test_device);
   assert(fallthrough_called);
   assert(!realtime_called);
   assert(!catchall_called);

   midi_register_catchall_callback(&test_device, catchall_callback);
   reset();
   assert(!anything_called());
   midi_device_input(&test_device, 3, 0xB0, 0, 1);
   midi_device_input(&test_device, 1, MIDI_CLOCK, 0, 0);
   midi_device_process(&test_device);
   assert(fallthrough_called);
   assert(!realtime_called);
   assert(catchall_called);

   reset();
   midi_register_realtime_callback(&test_device, realtime_callback);
   assert(!anything_called());
   midi_device_input(&test_device, 1, MIDI_CLOCK, 0, 0);
   midi_device_process(&test_device);
   assert(!fallthrough_called);
   assert(realtime_called);
   assert(catchall_called);

   reset();
   assert(!anything_called());
   midi_device_input(&test_device, 3, 0xB0, 0, 1);
   midi_device_input(&test_device, 1, MIDI_CLOCK, 0, 0);
   midi_device_process(&test_device);
   assert(fallthrough_called);
   assert(realtime_called);
   assert(catchall_called);

   reset();
   assert(!anything_called());
   //interspersed
   midi_device_input(&test_device, 1, 0xB0, 0, 0);
   midi_device_input(&test_device, 1, MIDI_CLOCK, 0, 0);
   midi_device_input(&test_device, 1, 0, 0, 0);
   midi_device_input(&test_device, 1, MIDI_START, 0, 0);
   midi_device_input(&test_device, 1, 1, 0, 0);
   midi_device_process(&test_device);
   assert(fallthrough_called);
   assert(realtime_called);
   assert(catchall_called);

   reset();
   midi_register_cc_callback(&test_device, cc_callback);
   assert(!anything_called());
   midi_device_input(&test_device, 3, 0xB0, 0, 1);
   midi_device_process(&test_device);
   assert(!fallthrough_called);
   assert(cc_called);
   assert(catchall_called);

   reset();
   assert(!anything_called());
   midi_device_input(&test_device, 1, 0xB0, 0, 0);
   midi_device_input(&test_device, 1, MIDI_CLOCK, 0, 0);
   midi_device_input(&test_device, 1, 0, 0, 0);
   midi_device_input(&test_device, 1, MIDI_START, 0, 0);
   midi_device_input(&test_device, 1, 1, 0, 0);
   midi_device_process(&test_device);
   assert(cc_called);
   assert(realtime_called);
   assert(catchall_called);

   reset();
   assert(!anything_called());
   midi_register_sysex_callback(&test_device, sysex_callback);
   midi_device_input(&test_device, 3, SYSEX_BEGIN, 0, SYSEX_END);
   midi_device_process(&test_device);
   assert(sysex_called);
   assert(catchall_called);
   assert(!fallthrough_called);

   reset();
   assert(!anything_called());
   midi_register_sysex_callback(&test_device, sysex_callback);
   midi_device_input(&test_device, 3, SYSEX_BEGIN, 0, 0);
   midi_device_input(&test_device, 1, SYSEX_END, 0, 0);
   midi_device_process(&test_device);
   assert(sysex_called == 2);
   assert(catchall_called);
   assert(!fallthrough_called);

   reset();
   assert(!anything_called());
   midi_register_sysex_callback(&test_device, sysex_callback);
   midi_device_input(&test_device, 1, SYSEX_BEGIN, 0, 0);
   midi_device_input(&test_device, 1, 0x34, 0, 0);
   midi_device_input(&test_device, 1, SYSEX_END, 0, 0);
   midi_device_process(&test_device);
   assert(sysex_called == 1);
   assert(catchall_called);
   assert(!fallthrough_called);

   //part of a message
   reset();
   assert(!anything_called());
   midi_device_input(&test_device, 1, 0xB0, 0, 0);
   midi_device_input(&test_device, 1, 0, 0, 0);
   midi_device_process(&test_device);
   assert(!anything_called());

   printf("\n\nTEST PASSED!\n\n");
   return 0;
}
