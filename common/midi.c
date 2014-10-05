////////////////////////////////////////////////////////////////////////////////
// MIDI handling
////////////////////////////////////////////////////////////////////////////////

#include "midi.h"

#include "storage.h"
#include "ui.h"
#include "uart_6850.h"
#include "import.h"
#include "arp.h"

#include "../xnormidi/midi_device.h"
#include "../xnormidi/midi.h"
#include "../xnormidi/bytequeue/bytequeue.h"

#define MAX_SYSEX_SIZE TEMP_BUFFER_SIZE

#define MIDI_BASE_STEPPED_CC 48
#define MIDI_BASE_COARSE_CC 16
#define MIDI_BASE_FINE_CC 80

static MidiDevice midi;
static int16_t sysexSize;
static byteQueue_t sendQueue;
static uint8_t sendQueueData[32];

extern void refreshFullState(void);
extern void refreshPresetMode(void);

uint16_t midiCombineBytes(uint8_t first, uint8_t second)
{
   uint16_t _14bit;
   _14bit = (uint16_t)second;
   _14bit <<= 7;
   _14bit |= (uint16_t)first;
   return _14bit;
}

static void sysexSend(uint8_t command, int16_t size)
{
	int16_t chunkCount,i;
	uint8_t chunk[4];
	
	BLOCK_INT
	{
		chunkCount=((size-1)>>2)+1;

		uart_send(0xf0);
		uart_send(SYSEX_ID_0);
		uart_send(SYSEX_ID_1);
		uart_send(SYSEX_ID_2);
		uart_send(command);

		for(i=0;i<chunkCount;++i)
		{
			memcpy(chunk,&tempBuffer[i<<2],4);

			uart_send(chunk[0]&0x7f);
			uart_send(chunk[1]&0x7f);
			uart_send(chunk[2]&0x7f);
			uart_send(chunk[3]&0x7f);
			uart_send(((chunk[0]>>7)&1) | ((chunk[1]>>6)&2) | ((chunk[2]>>5)&4) | ((chunk[3]>>4)&8));
		}

		uart_send(0xf7);
	}
}

static int16_t sysexDescrambleBuffer(int16_t start)
{
	int16_t chunkCount,i,out;
	uint8_t b;
	
	chunkCount=((sysexSize-start)/5)+1;
	out=start;

	for(i=0;i<chunkCount;++i)
	{
		memmove(&tempBuffer[out],&tempBuffer[i*5+start],4);
		
		b=tempBuffer[i*5+start+4];
		
		tempBuffer[out+0]|=(b&1)<<7;
		tempBuffer[out+1]|=(b&2)<<6;
		tempBuffer[out+2]|=(b&4)<<5;
		tempBuffer[out+3]|=(b&8)<<4;
		
		out+=4;
	}
	
	return out-start;
}

static void sysexReceiveByte(uint8_t b)
{
	int16_t size;

	switch(b)
	{
	case 0xF0:
		sysexSize=0;
		memset(tempBuffer,0,MAX_SYSEX_SIZE);
		break;
	case 0xF7:
		if(tempBuffer[0]==0x01 && tempBuffer[1]==0x02) // SCI P600 program dump
		{
			import_sysex(tempBuffer,sysexSize);
		}
		else if(tempBuffer[0]==SYSEX_ID_0 && tempBuffer[1]==SYSEX_ID_1 && tempBuffer[2]==SYSEX_ID_2) // my sysex ID
		{
			// handle my sysex commands
			
			switch(tempBuffer[3])
			{
			case SYSEX_COMMAND_BANK_A:
				size=sysexDescrambleBuffer(4);
				storage_import(tempBuffer[4],&tempBuffer[5],size-1);
				break;
			}
		}

		sysexSize=0;
		refreshFullState();
		break;
	default:
		if(sysexSize>=MAX_SYSEX_SIZE)
		{
#ifdef DEBUG
			print("Warning: sysex buffer overflow\n");
#endif
			sysexSize=0;
		}
		
		tempBuffer[sysexSize++]=b;
	}
}

static int8_t midiFilterChannel(uint8_t channel)
{
	return settings.midiReceiveChannel<0 || (channel&MIDI_CHANMASK)==settings.midiReceiveChannel;
}

static void midi_noteOnEvent(MidiDevice * device, uint8_t channel, uint8_t note, uint8_t velocity)
{
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi note on  ");
	phex(note);
	print("\n");
#endif

	assigner_assignNote(note,velocity!=0,(((uint32_t)velocity+1)<<9)-1);
}

static void midi_noteOffEvent(MidiDevice * device, uint8_t channel, uint8_t note, uint8_t velocity)
{
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi note off ");
	phex(note);
	print("\n");
#endif

	assigner_assignNote(note,0,0);
}

static void midi_ccEvent(MidiDevice * device, uint8_t channel, uint8_t control, uint8_t value)
{
	int16_t param;
	
	if(!midiFilterChannel(channel))
		return;
	
#ifdef DEBUG_
	print("midi cc ");
	phex(control);
	print(" value ");
	phex(value);
	print("\n");
#endif

	if(control==0 && value<=1 && settings.presetMode!=value) // coarse bank #
	{
		settings.presetMode=value;
		settings_save();
		refreshPresetMode();
		refreshFullState();
	}
	else if(control==1) // modwheel
	{
		synth_wheelEvent(0,value<<9,2,0);
	}
	else if(control==64) // hold pedal
	{
		assigner_holdEvent(value);
		return;
	}
	
	if(!settings.presetMode) // in manual mode CC changes would only conflict with pot scans...
		return;
	
	if(control>=MIDI_BASE_COARSE_CC && control<MIDI_BASE_COARSE_CC+cpCount)
	{
		param=control-MIDI_BASE_COARSE_CC;

		currentPreset.continuousParameters[param]&=0x01fc;
		currentPreset.continuousParameters[param]|=(uint16_t)value<<9;
		ui_setPresetModified(1);	
	}
	else if(control>=MIDI_BASE_FINE_CC && control<MIDI_BASE_FINE_CC+cpCount)
	{
		param=control-MIDI_BASE_FINE_CC;

		currentPreset.continuousParameters[param]&=0xfe00;
		currentPreset.continuousParameters[param]|=(uint16_t)value<<2;
		ui_setPresetModified(1);	
	}
	else if(control>=MIDI_BASE_STEPPED_CC && control<MIDI_BASE_STEPPED_CC+spCount)
	{
		param=control-MIDI_BASE_STEPPED_CC;
		
		currentPreset.steppedParameters[param]=value>>(7-steppedParametersBits[param]);
		ui_setPresetModified(1);	
	}

	if(ui_isPresetModified())
		refreshFullState();
}

static void midi_progChangeEvent(MidiDevice * device, uint8_t channel, uint8_t program)
{
	if(!midiFilterChannel(channel))
		return;

	if(settings.presetMode && program<100  && program!=settings.presetNumber)
	{
		if(preset_loadCurrent(program))
		{
			settings.presetNumber=program;
			ui_setPresetModified(0);	
			settings_save();		
			refreshFullState();
		}
	}
}

static void midi_pitchBendEvent(MidiDevice * device, uint8_t channel, uint8_t v1, uint8_t v2)
{
	if(!midiFilterChannel(channel))
		return;

	int16_t value;
	
	value=midiCombineBytes(v1,v2);
	value-=0x2000;
	value<<=2;
	
	synth_wheelEvent(value,0,1,0);
}

static void midi_sysexEvent(MidiDevice * device, uint16_t count, uint8_t b0, uint8_t b1, uint8_t b2)
{
	if(sysexSize)
		count=count-sysexSize-1;
	
	if(count>0)
		sysexReceiveByte(b0);
	
	if(count>1)
		sysexReceiveByte(b1);

	if(count>2)
		sysexReceiveByte(b2);
}

static void midi_realtimeEvent(MidiDevice * device, uint8_t event)
{
	synth_realtimeEvent(event);
}

static void midi_sendFunc(MidiDevice * device, uint16_t count, uint8_t b0, uint8_t b1, uint8_t b2)
{
	if(count>0)
		bytequeue_enqueue(&sendQueue,b0);
	
	if(count>1)
		bytequeue_enqueue(&sendQueue,b1);

	if(count>2)
		bytequeue_enqueue(&sendQueue,b2);
}


void midi_init(void)
{
	midi_device_init(&midi);
	midi_device_set_send_func(&midi,midi_sendFunc);
	midi_register_noteon_callback(&midi,midi_noteOnEvent);
	midi_register_noteoff_callback(&midi,midi_noteOffEvent);
	midi_register_cc_callback(&midi,midi_ccEvent);
	midi_register_progchange_callback(&midi,midi_progChangeEvent);
	midi_register_pitchbend_callback(&midi,midi_pitchBendEvent);
	midi_register_sysex_callback(&midi,midi_sysexEvent);
	midi_register_realtime_callback(&midi,midi_realtimeEvent);
	
	sysexSize=0;
	
	bytequeue_init(&sendQueue, sendQueueData, sizeof(sendQueueData));
}

void midi_update(void)
{
	midi_device_process(&midi);
	
	if(bytequeue_length(&sendQueue)>0)
	{
		uint8_t b;
		b=bytequeue_get(&sendQueue,0);
		bytequeue_remove(&sendQueue,1);
		uart_send(b);
	}
}

void midi_newData(uint8_t data)
{
	midi_device_input(&midi,1,&data);
}

void midi_dumpPresets(void)
{
	int8_t i;
	int16_t size=0;

	for(i=0;i<100;++i)
	{
		if(preset_loadCurrent(i))
		{
			storage_export(i,tempBuffer,&size);
			sysexSend(SYSEX_COMMAND_BANK_A,size);
		}
	}
}

void midi_sendNoteEvent(uint8_t note, int8_t gate, uint16_t velocity)
{
	if(gate)
		midi_send_noteon(&midi,settings.midiSendChannel,note,velocity>>9);
	else
		midi_send_noteoff(&midi,settings.midiSendChannel,note,velocity>>9);
}

void midi_sendWheelEvent(int16_t bend, uint16_t modulation, uint8_t mask)
{
	static int16_t lastBend=0;
	static uint16_t lastMod=0;
	
	if(mask&1 && (bend&0xfffc)!=(lastBend&0xfffc))
	{
		midi_send_pitchbend(&midi,settings.midiSendChannel,bend);
		lastBend=bend;
	}

	if(mask&2 && (modulation&0xfe00)!=(lastMod&0xfe00))
	{
		midi_send_cc(&midi,settings.midiSendChannel,1,modulation>>9);
		lastMod=modulation;
	}
}

void midi_sendSustainEvent(int8_t on)
{
	midi_send_cc(&midi,settings.midiSendChannel,64,on?0x7f:0x00);
}