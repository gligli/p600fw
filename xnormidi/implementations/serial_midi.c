#include "serial_midi.h"
#include <avr/interrupt.h>
#include "stdlib.h"

static MidiDevice midi_device;

void serial_midi_send(MidiDevice * device, uint8_t cnt, uint8_t inByte0, uint8_t inByte1, uint8_t inByte2){
   //we always send the first byte
	while ( !(UCSRA & _BV(UDRE)) ); // Wait for empty transmit buffer
	UDR = inByte0;
   //if cnt == 2 or 3 we send the send byte
   if(cnt > 1) {
      while ( !(UCSRA & _BV(UDRE)) ); // Wait for empty transmit buffer
      UDR = inByte1;
   }
   //if cnt == 3 we send the third byte
   if(cnt == 3) {
      while ( !(UCSRA & _BV(UDRE)) ); // Wait for empty transmit buffer
      UDR = inByte2;
   }
}

MidiDevice * serial_midi_device(void) {
   return &midi_device;
}

MidiDevice* serial_midi_init(uint16_t clockScale, bool out, bool in){
   //send up the device
   midi_device_init(&midi_device);
   midi_device_set_send_func(&midi_device, serial_midi_send);

	// Set baud rate
	UBRRH = (uint8_t)(clockScale >> 8);
	UBRRL = (uint8_t)(clockScale & 0xFF);
	// Enable transmitter
	if(out)
		UCSRB |= _BV(TXEN);
	if(in) {
		//Enable receiver
		//RX Complete Interrupt Enable  (user must provide routine)
		UCSRB |= _BV(RXEN) | _BV(RXCIE);
	}
	//Set frame format: Async, 8data, 1 stop bit, 1 start bit, no parity
	//needs to have URSEL set in order to write into this reg
	UCSRC = _BV(URSEL) | _BV(UCSZ1) | _BV(UCSZ0);

   return serial_midi_device();
}

