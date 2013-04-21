#include "midi_usb.h" //see the lufa_midi implementation

//forward declarations of callbacks
void fallthrough_callback(MidiDevice * device,
      uint16_t cnt, uint8_t byte0, uint8_t byte1, uint8_t byte2);
void cc_callback(MidiDevice * device,
      uint8_t chan, uint8_t num, uint8_t val);

int main(void) {
   MidiDevice midi_device;

   //setup the device
   midi_usb_init(&midi_device);

   //register callbacks
   midi_register_fallthrough_callback(&midi_device, fallthrough_callback);
   midi_register_cc_callback(&midi_device, cc_callback);

   //send some messages
   midi_send_cc(&midi_device, 0, 1, 2);
   midi_send_noteon(&midi_device, 0, 64, 127);
   midi_send_noteoff(&midi_device, 0, 64, 127);

   while(1){
      //processes input from the midi device
      //and calls the appropriate callbacks
      midi_device_process(&midi_device);
   }

   return 0; //never happens
}

//echo data back
void fallthrough_callback(MidiDevice * device,
      uint16_t cnt, uint8_t byte0, uint8_t byte1, uint8_t byte2){
   //pass the data back to the device, using the general purpose send data
   //function, any bytes after cnt are ignored
   midi_send_data(device, cnt, byte0, byte1, byte2);
}

void cc_callback(MidiDevice * device,
      uint8_t chan, uint8_t num, uint8_t val) {
   //sending it back on the next channel
   midi_send_cc(device, (chan + 1) % 16, num, val);
}
