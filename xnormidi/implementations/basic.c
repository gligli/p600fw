/*
 
	Here is an example of how to use avr-midi
	this was written for an atmega16 using a 12Mhz clock in an stk500
	the switch inputs of the STK500 are connected to PORTC

	switch 0 and 1 increment and decrement [respectively] a midi program and
	send the appropriate midi program change

	switch 2 sends a note on message when the button is pushed and a note off
	when the button is released

	the rest of the switches send CC messages, a 1 when the button is down and a
	0 when the button is released

*/

#include <avr/io.h>
#include <avr/interrupt.h>
#include <inttypes.h>
#include "serial_midi.h"

#define MIDI_CLOCK_12MHZ_OSC 23
#define MIDI_CLOCK_RATE MIDI_CLOCK_12MHZ_OSC

//how many times to we read the buttons before we say they're valid
#define INPUT_HISTORY 4

//this is the midi channel we'll be sending on.. 0 is midi channel 1
#define MIDI_CHAN 0

int main(void) {
	//index for looping
	uint8_t i, j;
	bool consistent;
	//is the button up
	bool up;
	//midi program 
	uint8_t program = 0;
	//button inputs, for debouncing
	uint8_t inputs[INPUT_HISTORY];
	uint8_t curInput = 0;
	uint8_t lastInput = 0;
	uint8_t historyIndex = 0;

	//PORTC is in input with internal pullups
	DDRC = 0x00;
	PORTC = 0xFF;

	//init the button history
	for(i = 0; i < INPUT_HISTORY; i++)
		inputs[i] = 0;

	//init midi, give the clock rate setting, indicate that we want only output
	MidiDevice * midi_device = serial_midi_init(MIDI_CLOCK_RATE, true, false);

	while(1){

		//grab the inputs
		inputs[historyIndex] = PINC;
		curInput = lastInput;

		//debounce inputs
		//for each input
		for(i = 0; i < 8; i++){
			consistent = true;
			//the STK500 has 1 for up and 0 for down if you're using the on board buttons
			up = (bool)(0x1 & (inputs[0] >> i));
			//check the history to make sure we have the same value for this input
			//for the whole history
			for(j = 1; j < INPUT_HISTORY; j++){
				if(up != (bool)(0x1 & (inputs[j] >> i))){
					consistent = false;
					break;
				}
			}
			//if we have the same value for the whole history then store it in
			//curInput
			if(consistent){
				if(up) {
					//store the current input state
					curInput |= (1 << i);
					//if the last input was a 0 and the current is a 1
					if(((lastInput >> i) & 0x1) == 0){
						//we don't do anything on up for the first two buttons
						//send the appropriate midi data
						if(i == 2){
							midi_send_noteoff(midi_device, MIDI_CHAN, 64, 30);
						} else if(i > 1)
							midi_send_cc(midi_device, MIDI_CHAN, i - 3, 0);
					}
				} else {
					//store the current input state
					curInput &= ~(1 << i);
					//if the last input was a 1 and the current is a 0
					if(((lastInput >> i) & 0x1) == 1){
						//send the appropriate midi data
						switch(i){
							case 0:
								program = (program + 1) % 128;
								midi_send_programchange(midi_device, MIDI_CHAN, program);
								break;
							case 1:
								program = (program - 1) % 128;
								midi_send_programchange(midi_device, MIDI_CHAN, program);
								break;
							case 2:
								midi_send_noteon(midi_device, MIDI_CHAN, 64, 127);
								break;
							default:
								midi_send_cc(midi_device, MIDI_CHAN, i - 3, 1);
								break;
						}
					}
				}
			}
		}

		//update the history index
		historyIndex = (historyIndex + 1) % INPUT_HISTORY;
		//store the curInput as lastInput for the next loop
		lastInput = curInput;
	}

	return 0;
}

