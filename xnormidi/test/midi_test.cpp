#include "midi_test.h"
#include <vector>
#include <streambuf>
#include <stdlib.h>
#include <cmath>

#include "midi.h"

using std::vector;

CPPUNIT_TEST_SUITE_REGISTRATION( MIDITest );

class CallbackInfo {
   public:
      CallbackInfo(std::string t, uint8_t byte0 = 0, uint8_t byte1 = 0, uint8_t byte2 = 0, uint16_t cnt = 0) :
         type(t) {
            bytes[0] = byte0;
            bytes[1] = byte1;
            bytes[2] = byte2;
            count = cnt;
         }
      std::string type;
      uint8_t bytes[3];
      uint16_t count;
};

static vector<CallbackInfo> callback_data;

extern "C" void cc_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("cc", byte0, byte1, byte2));
}
extern "C" void noteon_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("noteon", byte0, byte1, byte2));
}
extern "C" void noteoff_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("noteoff", byte0, byte1, byte2));
}
extern "C" void aftertouch_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("aftertouch", byte0, byte1, byte2));
}
extern "C" void pitchbend_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("pitchbend", byte0, byte1, byte2));
}
extern "C" void songposition_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("songposition", byte0, byte1, byte2));
}

extern "C" void progchange_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1){
   callback_data.push_back(CallbackInfo("progchange", byte0, byte1));
}
extern "C" void chanpressure_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1){
   callback_data.push_back(CallbackInfo("chanpressure", byte0, byte1));
}
extern "C" void songselect_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1){
   callback_data.push_back(CallbackInfo("songselect", byte0, byte1));
}
extern "C" void tc_quarterframe_callback(MidiDevice * device, uint8_t byte0, uint8_t byte1){
   callback_data.push_back(CallbackInfo("tc_quarterframe", byte0, byte1));
}

extern "C" void realtime_callback(MidiDevice * device, uint8_t byte){
   callback_data.push_back(CallbackInfo("realtime", byte));
}
extern "C" void tunerequest_callback(MidiDevice * device, uint8_t byte){
   callback_data.push_back(CallbackInfo("tunerequest", byte));
}

extern "C" void fallthrough_callback(MidiDevice * device, uint16_t cnt, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("fallthrough", byte0, byte1, byte2));
}

extern "C" void catchall_callback(MidiDevice * device, uint16_t cnt, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("catchall", byte0, byte1, byte2));
}

extern "C" void sysex_callback(MidiDevice * device, uint16_t cnt, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   callback_data.push_back(CallbackInfo("sysex", byte0, byte1, byte2, cnt));
}


void register_all_callbacks(MidiDevice * device, bool include_catchall = false) {
   midi_register_cc_callback(device, cc_callback);
   midi_register_noteon_callback(device, noteon_callback);
   midi_register_noteoff_callback(device, noteoff_callback);
   midi_register_aftertouch_callback(device, aftertouch_callback);
   midi_register_pitchbend_callback(device, pitchbend_callback);
   midi_register_songposition_callback(device, songposition_callback);

   midi_register_progchange_callback(device, progchange_callback);
   midi_register_chanpressure_callback(device, chanpressure_callback);
   midi_register_songselect_callback(device, songselect_callback);
   midi_register_tc_quarterframe_callback(device, tc_quarterframe_callback);

   midi_register_realtime_callback(device, realtime_callback);
   midi_register_tunerequest_callback(device, tunerequest_callback);

   midi_register_fallthrough_callback(device, fallthrough_callback);
   if(include_catchall)
      midi_register_catchall_callback(device, catchall_callback);
   midi_register_sysex_callback(device, sysex_callback);
}

void MIDITest::setUp() {
   callback_data.clear();
}

void MIDITest::statusByteTest() {
   const uint8_t status[] = {
      MIDI_CLOCK,
      MIDI_TICK,
      MIDI_START,
      MIDI_CONTINUE,
      MIDI_STOP,
      MIDI_ACTIVESENSE,
      MIDI_RESET,
      MIDI_CC,
      MIDI_NOTEON,
      MIDI_NOTEOFF,
      MIDI_AFTERTOUCH,
      MIDI_PITCHBEND,
      MIDI_PROGCHANGE,
      MIDI_CHANPRESSURE,
      MIDI_TC_QUARTERFRAME,
      MIDI_SONGPOSITION,
      MIDI_SONGSELECT,
      MIDI_TUNEREQUEST,
      SYSEX_BEGIN,
      SYSEX_END
   };

   for(unsigned int i = 0; i < 0x80; i++) {
      std::stringstream msg;
      msg << "testing: " << i;
      CPPUNIT_ASSERT_MESSAGE(msg.str(), !midi_is_statusbyte(i));
   }

   for(unsigned int i = 0; i < sizeof(status); i++) {
      std::stringstream msg;
      msg << "testing: " << status[i];
      CPPUNIT_ASSERT_MESSAGE(msg.str(), midi_is_statusbyte(status[i]));
   }
}

void MIDITest::realtimeTest() {
   const uint8_t realtime[] = {
      MIDI_CLOCK,
      MIDI_TICK,
      MIDI_START,
      MIDI_CONTINUE,
      MIDI_STOP,
      MIDI_ACTIVESENSE,
      MIDI_RESET
   };
   const uint8_t not_realtime[] = {
      MIDI_CC,
      MIDI_NOTEON,
      MIDI_NOTEOFF,
      MIDI_AFTERTOUCH,
      MIDI_PITCHBEND,
      MIDI_PROGCHANGE,
      MIDI_CHANPRESSURE,
      MIDI_TC_QUARTERFRAME,
      MIDI_SONGPOSITION,
      MIDI_SONGSELECT,
      MIDI_TUNEREQUEST,
      SYSEX_BEGIN,
      SYSEX_END,
      0,
      1,
      2,
      3,
      4,
      127
   };

   for (unsigned int i = 0; i < sizeof(realtime); i++) {
      std::stringstream msg;
      msg << "testing: " << realtime[i];
      CPPUNIT_ASSERT_MESSAGE(msg.str(), midi_is_realtime(realtime[i]));
   }

   for (unsigned int i = 0; i < sizeof(not_realtime); i++) {
      std::stringstream msg;
      msg << "testing: " << not_realtime[i];
      CPPUNIT_ASSERT_MESSAGE(msg.str(), !midi_is_realtime(not_realtime[i]));
   }
}

void MIDITest::packetLengthTest() {
   const uint8_t one_byte[] = {
      MIDI_CLOCK,
      MIDI_TICK,
      MIDI_START,
      MIDI_CONTINUE,
      MIDI_STOP,
      MIDI_ACTIVESENSE,
      MIDI_RESET,
      MIDI_TUNEREQUEST
   };

   const uint8_t two_byte[] = {
      MIDI_PROGCHANGE,
      MIDI_CHANPRESSURE,
      MIDI_SONGSELECT,
      MIDI_TC_QUARTERFRAME
   };

   const uint8_t three_byte[] = {
      MIDI_CC,
      MIDI_NOTEON,
      MIDI_NOTEOFF,
      MIDI_AFTERTOUCH,
      MIDI_PITCHBEND,
      MIDI_SONGPOSITION
   };

   for(unsigned int i = 0; i < sizeof(one_byte); i++) {
      const uint8_t status = one_byte[i];
      std::stringstream msg;
      msg << "testing: " << status;
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), ONE, midi_packet_length(status));
      //test channel messages
      if (status < 0xF) {
         for (unsigned int c = 0; c <= 0xF; c++)
            CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), ONE, midi_packet_length(status | c));
      }
   }

   for(unsigned int i = 0; i < sizeof(two_byte); i++) {
      const uint8_t status = two_byte[i];
      std::stringstream msg;
      msg << "testing: " << status;
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), TWO, midi_packet_length(status));
      //test channel messages
      if (status < 0xF) {
         for (unsigned int c = 0; c <= 0xF; c++)
            CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), TWO, midi_packet_length(status | c));
      }
   }
   
   for(unsigned int i = 0; i < sizeof(three_byte); i++) {
      const uint8_t status = three_byte[i];
      std::stringstream msg;
      msg << "testing: " << status;
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), THREE, midi_packet_length(status));
      //test channel messages
      if (status < 0xF) {
         for (unsigned int c = 0; c <= 0xF; c++)
            CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), THREE, midi_packet_length(status | c));
      }
   }

   CPPUNIT_ASSERT_EQUAL(UNDEFINED, midi_packet_length(SYSEX_BEGIN));
   CPPUNIT_ASSERT_EQUAL(UNDEFINED, midi_packet_length(SYSEX_END));
   for(uint8_t i = 0; i < 0x80; i++)
      CPPUNIT_ASSERT_EQUAL(UNDEFINED, midi_packet_length(i));
}

#define BUFFER_SIZE 1024
void MIDITest::threeByteCallbacks() {
   typedef void (*three_byte_callback_reg_t)(MidiDevice * device, midi_three_byte_func_t func);

   three_byte_callback_reg_t registrations[] = {
      midi_register_cc_callback,
      midi_register_noteon_callback,
      midi_register_noteoff_callback,
      midi_register_aftertouch_callback,
      midi_register_pitchbend_callback,
      midi_register_songposition_callback
   };

   midi_three_byte_func_t callbacks[] = {
      cc_callback,
      noteon_callback,
      noteoff_callback,
      aftertouch_callback,
      pitchbend_callback,
      songposition_callback
   };

   uint8_t status_bytes[] = {
      MIDI_CC,
      MIDI_NOTEON,
      MIDI_NOTEOFF,
      MIDI_AFTERTOUCH,
      MIDI_PITCHBEND,
      MIDI_SONGPOSITION
   };

   std::string cb_names[] = {
      "cc",
      "noteon",
      "noteoff",
      "aftertouch",
      "pitchbend",
      "songposition"
   };

   for (unsigned int cb_type = 0; cb_type < sizeof(status_bytes); cb_type++) {
      std::string msg = "failure processing: " + cb_names[cb_type];
      const uint8_t status = status_bytes[cb_type];
      callback_data.clear();

      MidiDevice device;
      midi_device_init(&device);

      uint8_t buffer[BUFFER_SIZE];
      buffer[0] = status;
      buffer[1] = 0x01;
      buffer[2] = 0x20;

      CPPUNIT_ASSERT_EQUAL(0, (int)callback_data.size());

      //nothing has been registered, should not get callback after processing
      midi_device_input(&device, 0, NULL);
      midi_device_input(&device, 3, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 0, (int)callback_data.size());

      //set up fall through registration
      midi_register_fallthrough_callback(&device, fallthrough_callback);
      midi_device_input(&device, 3, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 1, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("fallthrough"), callback_data.back().type);
      for(unsigned int i = 0; i < 3; i++)
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i], callback_data.back().bytes[i]);

      //set up a registration
      registrations[cb_type](&device, callbacks[cb_type]);
      midi_device_input(&device, 3, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 2, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, cb_names[cb_type], callback_data.back().type);
      for(unsigned int i = 0; i < 3; i++)
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i], callback_data.back().bytes[i]);

      //multiple messages between processing
      callback_data.clear();
      for (unsigned int i = 0; i < 16; i++) {
         if(status & 0x0F) {
            buffer[0 + i * 3] = status;
         } else
            buffer[0 + i * 3] = status | i;
         buffer[1 + i * 3] = rand() % 0x7f;
         buffer[2 + i * 3] = rand() % 0x7f;
         midi_device_input(&device, 3, buffer + i * 3);
      }
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 16, (int)callback_data.size());

      for (unsigned int i = 0; i < 16; i++) {
         for (unsigned int j = 0; j < 3; j++)
            CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i * 3 + j], callback_data[i].bytes[j]);
      }

      //test running status
      callback_data.clear();
      buffer[0] = status;
      midi_device_input(&device, 1, buffer);
      for (unsigned int i = 0; i < 16; i++) {
         buffer[i * 2] = rand() % 0x7f;
         buffer[1 + i * 2] = rand() % 0x7f;
         midi_device_input(&device, 2, buffer + i * 2);
      }
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 16, (int)callback_data.size());

      for (unsigned int i = 0; i < 16; i++) {
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, cb_names[cb_type], callback_data[i].type);
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, status, callback_data[i].bytes[0]);
         for (unsigned int j = 0; j < 2; j++)
            CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i * 2 + j], callback_data[i].bytes[j + 1]);
      }

      //register catchall
      buffer[0] = status;
      buffer[1] = 0x01;
      buffer[2] = 0x20;
      callback_data.clear();
      midi_register_catchall_callback(&device, catchall_callback);
      midi_device_input(&device, 3, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 2, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, cb_names[cb_type], callback_data[0].type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("catchall"), callback_data[1].type);
      for(unsigned int i = 0; i < 3; i++)
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i], callback_data.back().bytes[i]);
   }
}

void MIDITest::twoByteCallbacks() {
   typedef void (*two_byte_callback_reg_t)(MidiDevice * device, midi_two_byte_func_t func);

   const two_byte_callback_reg_t registrations[] = {
      midi_register_progchange_callback,
      midi_register_chanpressure_callback,
      midi_register_songselect_callback,
      midi_register_tc_quarterframe_callback
   };

   const midi_two_byte_func_t callbacks[] = {
      progchange_callback,
      chanpressure_callback,
      songselect_callback,
      tc_quarterframe_callback
   };

   const uint8_t status_bytes[] = {
      MIDI_PROGCHANGE,
      MIDI_CHANPRESSURE,
      MIDI_SONGSELECT,
      MIDI_TC_QUARTERFRAME
   };

   const std::string cb_names[] = {
      "progchange",
      "chanpressure",
      "songselect",
      "tc_quarterframe"
   };

   for (unsigned int cb_type = 0; cb_type < sizeof(status_bytes); cb_type++) {
      const uint8_t status = status_bytes[cb_type];
      std::string msg = "failure processing: " + cb_names[cb_type];
      callback_data.clear();

      MidiDevice device;
      midi_device_init(&device);

      uint8_t buffer[BUFFER_SIZE];
      buffer[0] = status;
      buffer[1] = 0x01;

      CPPUNIT_ASSERT_EQUAL(0, (int)callback_data.size());

      //nothing has been registered, should not get callback after processing
      midi_device_input(&device, 0, NULL);
      midi_device_input(&device, 2, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 0, (int)callback_data.size());

      //set up fall through registration
      midi_register_fallthrough_callback(&device, fallthrough_callback);
      midi_device_input(&device, 2, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 1, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("fallthrough"), callback_data.back().type);
      for(unsigned int i = 0; i < 2; i++)
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i], callback_data.back().bytes[i]);

      //set up a registration
      registrations[cb_type](&device, callbacks[cb_type]);
      midi_device_input(&device, 2, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 2, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, cb_names[cb_type], callback_data.back().type);
      for(unsigned int i = 0; i < 2; i++)
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i], callback_data.back().bytes[i]);

      //multiple messages between processing
      callback_data.clear();
      for (unsigned int i = 0; i < 16; i++) {
         if(status & 0x0F) {
            buffer[0 + i * 2] = status;
         } else
            buffer[0 + i * 2] = status | i;
         buffer[1 + i * 2] = rand() % 0x7f;
         midi_device_input(&device, 2, buffer + i * 2);
      }
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 16, (int)callback_data.size());

      for (unsigned int i = 0; i < 16; i++) {
         for (unsigned int j = 0; j < 2; j++)
            CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i * 2 + j], callback_data[i].bytes[j]);
      }

      //test running status
      callback_data.clear();
      buffer[0] = status;
      midi_device_input(&device, 1, buffer);
      for (unsigned int i = 0; i < 16; i++) {
         buffer[i] = rand() % 0x7f;
         midi_device_input(&device, 1, buffer + i);
      }
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 16, (int)callback_data.size());

      for (unsigned int i = 0; i < 16; i++) {
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, cb_names[cb_type], callback_data[i].type);
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, status, callback_data[i].bytes[0]);
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i], callback_data[i].bytes[1]);
      }

      //register catchall
      buffer[0] = status;
      buffer[1] = 0x01;
      callback_data.clear();
      midi_register_catchall_callback(&device, catchall_callback);
      midi_device_input(&device, 2, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 2, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, cb_names[cb_type], callback_data[0].type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("catchall"), callback_data[1].type);
      for(unsigned int i = 0; i < 2; i++)
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[i], callback_data.back().bytes[i]);
   }
}

void MIDITest::oneByteCallbacks() {
   const uint8_t onebyte[] = {
      MIDI_CLOCK,
      MIDI_TICK,
      MIDI_START,
      MIDI_CONTINUE,
      MIDI_STOP,
      MIDI_ACTIVESENSE,
      MIDI_RESET
   };
   for(unsigned int i = 0; i < sizeof(onebyte); i++) {
      const uint8_t status = onebyte[i];
      std::string msg = "failure processing: " + status;
      callback_data.clear();

      MidiDevice device;
      midi_device_init(&device);

      uint8_t buffer[BUFFER_SIZE];
      buffer[0] = status;

      CPPUNIT_ASSERT_EQUAL(0, (int)callback_data.size());

      //nothing has been registered, should not get callback after processing
      midi_device_input(&device, 0, NULL);
      midi_device_input(&device, 1, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 0, (int)callback_data.size());

      //set up fall through registration
      midi_register_fallthrough_callback(&device, fallthrough_callback);
      midi_device_input(&device, 1, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 1, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("fallthrough"), callback_data.back().type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[0], callback_data.back().bytes[0]);

      //set up a registration
      midi_register_realtime_callback(&device, realtime_callback);
      midi_device_input(&device, 1, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 2, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("realtime"), callback_data.back().type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[0], callback_data.back().bytes[0]);

      //register catchall
      midi_register_catchall_callback(&device, catchall_callback);
      midi_device_input(&device, 1, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 4, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("realtime"), callback_data[2].type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("catchall"), callback_data[3].type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[0], callback_data.back().bytes[0]);
   }

   {
      const uint8_t status = MIDI_TUNEREQUEST;
      std::string msg = "failure processing: " + status;
      callback_data.clear();

      MidiDevice device;
      midi_device_init(&device);

      uint8_t buffer[BUFFER_SIZE];
      buffer[0] = status;

      CPPUNIT_ASSERT_EQUAL(0, (int)callback_data.size());

      //nothing has been registered, should not get callback after processing
      midi_device_input(&device, 0, NULL);
      midi_device_input(&device, 1, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 0, (int)callback_data.size());

      //set up fall through registration
      midi_register_fallthrough_callback(&device, fallthrough_callback);
      midi_device_input(&device, 1, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 1, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("fallthrough"), callback_data.back().type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[0], callback_data.back().bytes[0]);

      //set up a registration
      midi_register_tunerequest_callback(&device, tunerequest_callback);
      midi_device_input(&device, 1, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 2, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("tunerequest"), callback_data.back().type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[0], callback_data.back().bytes[0]);

      //register catchall
      midi_register_catchall_callback(&device, catchall_callback);
      midi_device_input(&device, 1, buffer);
      midi_device_process(&device);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, 4, (int)callback_data.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("tunerequest"), callback_data[2].type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, std::string("catchall"), callback_data[3].type);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg, buffer[0], callback_data.back().bytes[0]);
   }
}

void MIDITest::sysexCallback() {
   MidiDevice device;
   midi_device_init(&device);
   register_all_callbacks(&device);

   //the internal buffer is 192 bytes long.. that should probably be configurable..
   for(unsigned int len = 0; len < 185; len++) {
      std::stringstream msg;
      callback_data.clear();

      uint8_t buffer[BUFFER_SIZE];
      buffer[0] = SYSEX_BEGIN;
      for (unsigned int i = 0; i < len; i++)
         buffer[i + 1] = i % 0x7F;
      buffer[len + 1] = SYSEX_END;

      midi_device_input(&device, 2 + len, buffer);
      midi_device_process(&device);
      unsigned int num = (unsigned int)ceil(((float)len + 2.0) / 3.0);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), num, (unsigned int)callback_data.size());

      for (unsigned int i = 0; i < num; i++) {
         unsigned int cnt = i * 3 + 3;
         if (cnt >= len + 2)
            cnt = len + 2;
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), std::string("sysex"), callback_data[i].type);
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), cnt, (unsigned int)callback_data[i].count);
      }

      for (unsigned int i = 0; i < len + 2; i++)
         CPPUNIT_ASSERT_EQUAL_MESSAGE(msg.str(), (unsigned int)buffer[i], (unsigned int)callback_data[i / 3].bytes[i % 3]);
   }
}

void MIDITest::interspersedRealtime() {
   //test a bunch of messages with realtime interspersed
   uint8_t buffer[] = {
      MIDI_TICK,
      MIDI_CC, 0x1, 0x2,
      MIDI_NOTEON, 0x2, MIDI_CLOCK, 0x3,
      MIDI_STOP,
      MIDI_NOTEOFF, MIDI_START, 0x4, 0x6,
      MIDI_AFTERTOUCH, MIDI_CONTINUE, 0x7, 0x8,
      MIDI_RESET, MIDI_ACTIVESENSE
   };

   MidiDevice device;
   midi_device_init(&device);
   register_all_callbacks(&device);
   midi_device_input(&device, sizeof(buffer), buffer);

   midi_device_process(&device);
   CPPUNIT_ASSERT_EQUAL(11, (int)callback_data.size());

   CPPUNIT_ASSERT_EQUAL(std::string("realtime"), callback_data[0].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_TICK, (int)callback_data[0].bytes[0]);

   CPPUNIT_ASSERT_EQUAL(std::string("cc"), callback_data[1].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_CC, (int)callback_data[1].bytes[0]);
   CPPUNIT_ASSERT_EQUAL(0x1, (int)callback_data[1].bytes[1]);
   CPPUNIT_ASSERT_EQUAL(0x2, (int)callback_data[1].bytes[2]);

   CPPUNIT_ASSERT_EQUAL(std::string("realtime"), callback_data[2].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_CLOCK, (int)callback_data[2].bytes[0]);

   CPPUNIT_ASSERT_EQUAL(std::string("noteon"), callback_data[3].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_NOTEON, (int)callback_data[3].bytes[0]);
   CPPUNIT_ASSERT_EQUAL(0x2, (int)callback_data[3].bytes[1]);
   CPPUNIT_ASSERT_EQUAL(0x3, (int)callback_data[3].bytes[2]);

   CPPUNIT_ASSERT_EQUAL(std::string("realtime"), callback_data[4].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_STOP, (int)callback_data[4].bytes[0]);

   CPPUNIT_ASSERT_EQUAL(std::string("realtime"), callback_data[5].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_START, (int)callback_data[5].bytes[0]);

   CPPUNIT_ASSERT_EQUAL(std::string("noteoff"), callback_data[6].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_NOTEOFF, (int)callback_data[6].bytes[0]);
   CPPUNIT_ASSERT_EQUAL(0x4, (int)callback_data[6].bytes[1]);
   CPPUNIT_ASSERT_EQUAL(0x6, (int)callback_data[6].bytes[2]);

   CPPUNIT_ASSERT_EQUAL(std::string("realtime"), callback_data[7].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_CONTINUE, (int)callback_data[7].bytes[0]);

   CPPUNIT_ASSERT_EQUAL(std::string("aftertouch"), callback_data[8].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_AFTERTOUCH, (int)callback_data[8].bytes[0]);
   CPPUNIT_ASSERT_EQUAL(0x7, (int)callback_data[8].bytes[1]);
   CPPUNIT_ASSERT_EQUAL(0x8, (int)callback_data[8].bytes[2]);

   CPPUNIT_ASSERT_EQUAL(std::string("realtime"), callback_data[9].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_RESET, (int)callback_data[9].bytes[0]);

   CPPUNIT_ASSERT_EQUAL(std::string("realtime"), callback_data[10].type);
   CPPUNIT_ASSERT_EQUAL(MIDI_ACTIVESENSE, (int)callback_data[10].bytes[0]);
}


