/*
 * Modified by Alex Norman (x37v.info) from Dean Camera's MIDI LUFA demo
 * Original Copyright below
 */

/*
   LUFA Library
   Copyright (C) Dean Camera, 2010.

   dean [at] fourwalledcubicle [dot] com
   www.lufa-lib.org
   */

/*
   Copyright 2010  Dean Camera (dean [at] fourwalledcubicle [dot] com)

   Permission to use, copy, modify, distribute, and sell this
   software and its documentation for any purpose is hereby granted
   without fee, provided that the above copyright notice appear in
   all copies and that both that the copyright notice and this
   permission notice and warranty disclaimer appear in supporting
   documentation, and that the name of the author not be used in
   advertising or publicity pertaining to distribution of the
   software without specific, written prior permission.

   The author disclaim all warranties with regard to this
   software, including all implied warranties of merchantability
   and fitness.  In no event shall the author be liable for any
   special, indirect or consequential damages or any damages
   whatsoever resulting from loss of use, data or profits, whether
   in an action of contract, negligence or other tortious action,
   arising out of or in connection with the use or performance of
   this software.
   */

#include "midi_usb.h"

#include <avr/io.h>
#include <avr/wdt.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <stdbool.h>

#include "Descriptors.h"

#include <LUFA/Version.h>
#include <LUFA/Drivers/USB/USB.h>

/* Function Prototypes: */
void SetupHardware(void);

void EVENT_USB_Device_Connect(void);
void EVENT_USB_Device_Disconnect(void);
void EVENT_USB_Device_ConfigurationChanged(void);
void EVENT_USB_Device_ControlRequest(void);

/** LUFA MIDI Class driver interface configuration and state information. This structure is
 *  passed to all MIDI Class driver functions, so that multiple instances of the same class
 *  within a device can be differentiated from one another.
 */

USB_ClassInfo_MIDI_Device_t USB_MIDI_Interface =
{
   .Config =
   {
      .StreamingInterfaceNumber = 1,
      .DataINEndpoint           =
      {
         .Address          = MIDI_STREAM_IN_EPADDR,
         .Size             = MIDI_STREAM_EPSIZE,
         .Banks            = 1,
      },
      .DataOUTEndpoint          =
      {
         .Address          = MIDI_STREAM_OUT_EPADDR,
         .Size             = MIDI_STREAM_EPSIZE,
         .Banks            = 1,
      },
   },
};

#define SYSEX_START_OR_CONT 0x4
#define SYSEX_ENDS_IN_1 0x5
#define SYSEX_ENDS_IN_2 0x6
#define SYSEX_ENDS_IN_3 0x7

#define SYS_COMMON_1 0x5
#define SYS_COMMON_2 0x2
#define SYS_COMMON_3 0x3

void usb_send_func(MidiDevice * device, uint16_t cnt, uint8_t byte0, uint8_t byte1, uint8_t byte2) {
   MIDI_EventPacket_t event;
   event.Data1 = byte0;
   event.Data2 = byte1;
   event.Data3 = byte2;

   //if the length is undefined we assume it is a SYSEX message
   if (midi_packet_length(byte0) == UNDEFINED) {
      switch(cnt) {
         case 3:
            if (byte2 == SYSEX_END)
               event.Event = MIDI_EVENT(0, SYSEX_ENDS_IN_3);
            else
               event.Event = MIDI_EVENT(0, SYSEX_START_OR_CONT);
            break;
         case 2:
            if (byte1 == SYSEX_END)
               event.Event = MIDI_EVENT(0, SYSEX_ENDS_IN_2);
            else
               event.Event = MIDI_EVENT(0, SYSEX_START_OR_CONT);
            break;
         case 1:
            if (byte0 == SYSEX_END)
               event.Event = MIDI_EVENT(0, SYSEX_ENDS_IN_1);
            else
               event.Event = MIDI_EVENT(0, SYSEX_START_OR_CONT);
            break;
         default:
            return; //invalid cnt
      }
   } else {
      //deal with 'system common' messages
      //TODO are there any more?
      switch(byte0 & 0xF0){
         case MIDI_SONGPOSITION:
            event.Event = MIDI_EVENT(0, SYS_COMMON_3);
            break;
         case MIDI_SONGSELECT:
         case MIDI_TC_QUARTERFRAME:
            event.Event = MIDI_EVENT(0, SYS_COMMON_2);
            break;
         default:
            event.Event = MIDI_EVENT(0, byte0);
            break;
      }
   }

   MIDI_Device_SendEventPacket(&USB_MIDI_Interface, &event);
   MIDI_Device_Flush(&USB_MIDI_Interface);
   MIDI_Device_USBTask(&USB_MIDI_Interface);
   USB_USBTask();
}

void usb_get_midi(MidiDevice * device) {
   MIDI_EventPacket_t event;
   while (MIDI_Device_ReceiveEventPacket(&USB_MIDI_Interface, &event)) {

      midi_packet_length_t length = midi_packet_length(event.Data1);

      //pass the data to the device input function
      //not dealing with sysex yet
      if (length != UNDEFINED) {
         uint8_t input[3];
         input[0] = event.Data1;
         input[1] = event.Data2;
         input[2] = event.Data3;
         midi_device_input(device, length, input);
      }

   }
   MIDI_Device_USBTask(&USB_MIDI_Interface);
   USB_USBTask();
}

void midi_usb_init(MidiDevice * device){
   midi_device_init(device);
   midi_device_set_send_func(device, usb_send_func);
   midi_device_set_pre_input_process_func(device, usb_get_midi);

   SetupHardware();
   sei();
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
void SetupHardware(void)
{
   /* Disable watchdog if enabled by bootloader/fuses */
   MCUSR &= ~(1 << WDRF);
   wdt_disable();

   /* Disable clock division */
   clock_prescale_set(clock_div_1);

   /* Hardware Initialization */
   USB_Init();
}

/** Event handler for the library USB Connection event. */
void EVENT_USB_Device_Connect(void)
{
   //set some LED?
}

/** Event handler for the library USB Disconnection event. */
void EVENT_USB_Device_Disconnect(void)
{
   //set some LED?
}

/** Event handler for the library USB Configuration Changed event. */
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	ConfigSuccess &= MIDI_Device_ConfigureEndpoints(&USB_MIDI_Interface);
   //set some LED?
}

/** Event handler for the library USB Control Request reception event. */
void EVENT_USB_Device_ControlRequest(void)
{
	MIDI_Device_ProcessControlRequest(&USB_MIDI_Interface);
}

