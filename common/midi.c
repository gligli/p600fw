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

static void sendEnqueue(uint8_t b)
{
	for(;;)
	{
		if(bytequeue_enqueue(&sendQueue,b))
			break;
		midi_update(1);
	}
}

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

		sendEnqueue(0xf0);
		sendEnqueue(SYSEX_ID_0);
		sendEnqueue(SYSEX_ID_1);
		sendEnqueue(SYSEX_ID_2);
		sendEnqueue(command);

		for(i=0;i<chunkCount;++i)
		{
			memcpy(chunk,&tempBuffer[i<<2],4);

			sendEnqueue(chunk[0]&0x7f);
			sendEnqueue(chunk[1]&0x7f);
			sendEnqueue(chunk[2]&0x7f);
			sendEnqueue(chunk[3]&0x7f);
			sendEnqueue(((chunk[0]>>7)&1) | ((chunk[1]>>6)&2) | ((chunk[2]>>5)&4) | ((chunk[3]>>4)&8));
		}

		sendEnqueue(0xf7);
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

typedef struct {
	uint8_t semitone;
	uint8_t semitone_fraction_one;
	uint8_t semitone_fraction_two;
} semitone_t;

static void mtsReceiveBulkTuningDump(uint8_t * buf, int16_t size) // imogen: this function is not used anf there is no clear use case for it - maybe it should be removed
{
	if (size!=402) {
#ifdef DEBUG
	print("ERROR: in mtsReceiveBulkTuningDump(), size should be 402, but its "); phex16(size); print("\n");
#endif
		return;
	}
	
	semitone_t *semitones = (semitone_t *)&buf[17]; // 128 in length	

	// FIXME: do something with these...
	// uint8_t tuningProgramNumber = buf[0]; // 0-127, 'tuning program'
	// uint8_t checksum = buf[size-1];
	
	double fractionalSemitones;
	semitone_t *semitone;
	uint16_t fractionalComponent; // fraction of semitone, in .0061-cent units as per MTS
	int i;
	
#ifdef DEBUG
	char * tuningName = (char *)&buf[1]; // 16 byte 'tuning name'	
	
	print("Loading tuning: '");
	for(i=0; i < 16; i++) {
		pchar(tuningName[i]);
	}
	print("'\n");
#endif
	
	const double MTS_CENTS_PER_STEP = 0.006103515625; // == 100 / pow(2,14)
	
	for (i=0; i < 128; i++) {
		semitone = &semitones[i];
		fractionalComponent = (semitone->semitone_fraction_one << 7) + semitone->semitone_fraction_two;
		fractionalSemitones = ((double)semitone->semitone) + (MTS_CENTS_PER_STEP * fractionalComponent);
		tuner_setNoteTuning(i, fractionalSemitones);
	}

}

static void sysexReceiveByte(uint8_t b)
{
	int16_t size;

	switch(b)
	{
	case 0xF0: // Begin SysEx message
		sysexSize=0;
		memset(tempBuffer,0,MAX_SYSEX_SIZE);
		break;
	case 0xF7: // End SysEx message
		if(tempBuffer[0]==0x01 && tempBuffer[1]==0x02) // SCI P600 program dump
		{
			if (ui.isInPatchManagement)
			{
				import_sysex(tempBuffer,sysexSize);
			}
		}
		else if(tempBuffer[0]==SYSEX_ID_0 && tempBuffer[1]==SYSEX_ID_1 && tempBuffer[2]==SYSEX_ID_2) // my sysex ID
		{
			// handle my sysex commands
			
			switch(tempBuffer[3])
			{
			case SYSEX_COMMAND_PATCH_DUMP:
				size=sysexDescrambleBuffer(4);
				storage_import(tempBuffer[4],&tempBuffer[5],size-1);
				break;
			case SYSEX_COMMAND_PATCH_DUMP_REQUEST:
				midi_dumpPreset(tempBuffer[4]);
				break;
			}
		}
		else if(tempBuffer[0]==SYSEX_ID_UNIVERSAL_NON_REALTIME) // imogen: if SysEx tuning data usage is removed (see above), this part will be obsolete as well  
		{
			switch(tempBuffer[2])
			{
			case SYSEX_SUBID1_BULK_TUNING_DUMP:			
				switch(tempBuffer[3])
				{
					case SYSEX_SUBID2_BULK_TUNING_DUMP:
						// We've received an MTS bulk tuning dump
						mtsReceiveBulkTuningDump(&tempBuffer[4],sysexSize-4);
					break;
					case SYSEX_SUBID2_BULK_TUNING_DUMP_REQUEST:
						// TODO: send a sysex MTS with our current tuning 
					break;
				}
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

    synth_keyEvent(note, velocity!=0, 0, (((uint32_t)velocity+1)<<9)-1);
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

    synth_keyEvent(note,0,0,0);
}

static void midi_ccEvent(MidiDevice * device, uint8_t channel, uint8_t control, uint8_t value)
{
	int16_t param;
	int8_t change=0;
	
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
		synth_wheelEvent(0,value<<9,2,0,0);
	}
	else if(control==7) // added midi volume
    {
        synth_volEvent(((uint16_t)value)<<9);
    }
	else if(control==64) // hold pedal
	{
        synth_holdEvent(value, 0, 0); // the distinction between Unison and Poly mode will be handled there
		return;
	}
	else if(control==120) // All Sound off
    {
        if (value==127)
        {
            int8_t v;
            for (v=0;v<SYNTH_VOICE_COUNT;++v)
            {
                assigner_voiceDone(v);
            }
        }
    }
    else if(control==122) // Local Keyboard ON/OFF
    {
        if (value<=63) // local ON
        {
            ui_setLocalMode(0);
        }
        else if (value>=64) // local OFF
        {
            ui_setLocalMode(1);
        }
    }
    else if(control==123) // All Notes off
    {
        if (value==0) assigner_allKeysOff();
    }
    else if(control>=MIDI_BASE_COARSE_CC) // arp / seq clock coarse
	{
		if (control-MIDI_BASE_COARSE_CC==cpSeqArpClock)
		{
			if((settings.seqArpClock>>9)!=value)
			{
				settings.seqArpClock&=0x01fc;
				settings.seqArpClock|=(uint16_t)value<<9;
			}
			return;
		}
	}
	else if(control>=MIDI_BASE_FINE_CC) // arp / seq clock fine
	{
		if (control-MIDI_BASE_FINE_CC==cpSeqArpClock)
		{
			if(((settings.seqArpClock>>2)&0x7f)!=value)
			{
				settings.seqArpClock&=0xfe00;
				settings.seqArpClock|=(uint16_t)value<<2;
			}
			return;
		}
	}

	if(!settings.presetMode) // in manual mode CC changes would only conflict with pot scans...
		return;
	
	if(control>=MIDI_BASE_COARSE_CC && control<MIDI_BASE_COARSE_CC+cpCount)
	{
		param=control-MIDI_BASE_COARSE_CC;
		
		if((currentPreset.continuousParameters[param]>>9)!=value)
		{
			currentPreset.continuousParameters[param]&=0x01fc;
			currentPreset.continuousParameters[param]|=(uint16_t)value<<9;
			change=1;
		}
	}
	else if(control>=MIDI_BASE_FINE_CC && control<MIDI_BASE_FINE_CC+cpCount)
	{
		param=control-MIDI_BASE_FINE_CC;

		if(((currentPreset.continuousParameters[param]>>2)&0x7f)!=value)
		{
			currentPreset.continuousParameters[param]&=0xfe00;
			currentPreset.continuousParameters[param]|=(uint16_t)value<<2;
			change=1;
        }
	}
	else if(control>=MIDI_BASE_STEPPED_CC && control<MIDI_BASE_STEPPED_CC+spCount)
	{
		param=control-MIDI_BASE_STEPPED_CC;
		uint8_t v, prev;
		
        prev=currentPreset.steppedParameters[param];
        v=(((uint16_t)value)*steppedParameterRange[param])>>7;
		
		if(currentPreset.steppedParameters[param]!=v)
		{
			currentPreset.steppedParameters[param]=v;
			change=1;	
		}
		
		// special case for unison (pattern latch)
		
		if(param==spUnison) // the switch to unison
		{
			if(v && !prev)
			{
                // only execute changes into unison mode, not additional latches
				assigner_latchPattern(0);
			}
			else
			{
				assigner_setPoly();
			}
			assigner_getPattern(currentPreset.voicePattern,NULL);
		}
	}


	if(change)
	{
		ui_setPresetModified(1);
		refreshFullState();
	}
}

static void midi_progChangeEvent(MidiDevice * device, uint8_t channel, uint8_t program)
{
	if(!midiFilterChannel(channel))
		return;

	if(settings.presetMode && program<100  && program!=settings.presetNumber)
	{
		if (preset_checkPage(program)) // ignore MIDI prog change if the page is not valid
		{
			preset_loadCurrent(program,0);
			settings.presetNumber=program;
			ui_setPresetModified(0);
			refreshFullState();
			ui_setNoActivePot(1);
			ui.presetModified=0;
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
	
	synth_wheelEvent(value,0,1,0,0);
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
		sendEnqueue(b0);
	
	if(count>1)
		sendEnqueue(b1);

	if(count>2)
		sendEnqueue(b2);
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

void midi_update(int8_t onlySend)
{
	if(!onlySend)
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

uint8_t midi_dumpPreset(int8_t number)
{
	int16_t size=0;
	
	if(number<0 || number>99)
		return 0;

    if(preset_checkPage(number))
    {
        storage_export(number,tempBuffer,&size);
        sysexSend(SYSEX_COMMAND_PATCH_DUMP,size);
		return 1;
    }
    return 0;
}

void midi_dumpPresets(void)
{
	int8_t i;
	int16_t size=0;

	for(i=0;i<100;++i)
	{
		if(preset_loadCurrent(i,0))
		{
			storage_export(i,tempBuffer,&size);
			sysexSend(SYSEX_COMMAND_PATCH_DUMP,size);
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

void midi_sendProgChange(uint8_t prog)
{
    midi_send_programchange(&midi, settings.midiSendChannel, prog);
}


void midi_sendWheelEvent(int16_t bend, uint16_t modulation, uint8_t mask)
{
	static int16_t lastBend=0;
	static uint16_t lastMod=0;
	
	if(mask&1 && (bend&0xfffc)!=(lastBend&0xfffc)) // this avois too many events, e.g. only change or more than 3 (out 8192) triggers an event
	{
		//midi_send_pitchbend(&midi,settings.midiSendChannel,bend);
		lastBend=bend;

        uint16_t uAmt;
        uint8_t lsb, msb;

        uAmt=(bend-INT16_MIN); // shifts to range 0 ... UNIT16_MAX
        uAmt=uAmt>>2; // bitshift makes it a 14bit number, right shift is of logic type on UNIT
        //uAmt&=0x3FFF; // force the two highest bits to zero

        lsb=uAmt;
        lsb&=0x7F; // LSB: project onto the lowest 7 bits
        msb=(uAmt>>7); // MSB: shift down by 7 bits

        midi.send_func(&midi, 3, MIDI_PITCHBEND | (settings.midiSendChannel & MIDI_CHANMASK), lsb, msb); // midi_var_byte_func_t with count 3
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

void midi_sendThreeBytes(uint8_t mdchn, uint16_t val)
{
    uint8_t lsb, msb;

    lsb=val&0x7F;
    msb=(val>>7)&0x7F;

    midi.send_func(&midi, 3, MIDI_PITCHBEND | (mdchn & MIDI_CHANMASK), lsb, msb);
}
