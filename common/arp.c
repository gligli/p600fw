////////////////////////////////////////////////////////////////////////////////
// Arpeggiator
////////////////////////////////////////////////////////////////////////////////

#include "arp.h"

#include "scanner.h"
#include "assigner.h"
#include "storage.h"
#include "midi.h"
#include "clock.h"
#include "seq.h"

#define ARP_NOTE_MEMORY 128 // must stay>=128 for up/down mode

// this flag is used to indicate which of the current arp notes are latched so that these can be
// removed when latch mode is deactivated while the currently held keys continue to be played:
#define ARP_NOTE_HELD_FLAG 0x80

#define ARP_LAST_NOTE (ARP_NOTE_MEMORY-1)

static struct
{
	uint8_t notes[ARP_NOTE_MEMORY];
	int16_t noteIndex;
	int16_t previousIndex; // this is used to avoid multiple successive playing of notes (or indices) in random arp
	uint8_t previousNote;
	int8_t transpose,previousTranspose;

	int8_t hold;
	arpMode_t mode;
} arp;

static int8_t isEmpty(void)
{
	int16_t i;
	
	for(i=0;i<ARP_NOTE_MEMORY;++i)
		if(arp.notes[i]!=ASSIGNER_NO_NOTE)
			return 0;

	return 1;
}

static void finishPreviousNote(void)
{
	if(arp.previousNote!=ASSIGNER_NO_NOTE)
	{
		uint8_t n=arp.previousNote&~ARP_NOTE_HELD_FLAG; // remove the HELD bit
		
		assigner_assignNote(n+SCANNER_BASE_NOTE+arp.previousTranspose,0,0,0);
		
		// pass to MIDI out
		if (settings.midiMode==0) midi_sendNoteEvent(n+SCANNER_BASE_NOTE+arp.previousTranspose,0,0);
	}
}

static void killAllNotes(void)
{
	finishPreviousNote();

	arp.noteIndex=-1;
	arp.previousIndex=-1;
	arp.previousNote=ASSIGNER_NO_NOTE;

	memset(arp.notes,ASSIGNER_NO_NOTE,ARP_NOTE_MEMORY);
	assigner_allKeysOff();
}

static void killHeldNotes(void)
{
	int16_t i;
	for(i=0;i<ARP_NOTE_MEMORY;++i)
		if(arp.notes[i]&ARP_NOTE_HELD_FLAG)
			arp.notes[i]=ASSIGNER_NO_NOTE;
	
	// gate off for last note

	if(isEmpty())
		finishPreviousNote();
}

inline void arp_setMode(arpMode_t mode, int8_t hold)
{
	// stop previous assigned notes

	if(mode!=arp.mode)
	{
		if ((mode==amRandom && arp.mode==amAssign) || (arp.mode==amRandom && mode==amAssign))
		{
			// any action here?
		}
		else
		{
			killAllNotes();
			if (mode!=amOff)
				arp_resetCounter(settings.syncMode==smInternal);
		}
	}

	if(!hold && arp.hold)
		killHeldNotes();

	arp.mode=mode;
	arp.hold=(mode==amOff)?0:hold;
}

FORCEINLINE void arp_setTranspose(int8_t transpose)
{
	arp.transpose=transpose;
}

FORCEINLINE void arp_resetCounter(int8_t beatReset)
{
	arp.noteIndex=-1; // reinit
	if (beatReset&&seq_getMode(0)!=smPlaying&&seq_getMode(1)!=smPlaying)
    {
		clock_reset(); // start immediately
		synth_resetClockBar(); // reset the LFO sync counter
    }
}

FORCEINLINE arpMode_t arp_getMode(void)
{
	return arp.mode;
}

FORCEINLINE int8_t arp_getHold(void)
{
	return arp.hold;
}

void arp_assignNote(uint8_t note, int8_t on)
{
	int16_t i;
	
	if(arp.mode==amOff)
		return;
	
	// We only arpeggiate from the internal keyboard, so we can keep the
	// note memory size at 128 if we set the keyboard range to 0 and up.
	note-=SCANNER_BASE_NOTE;
	if(on)
	{

		// if this is the first note, make sure the arp will start on it as as soon as we update
		if(isEmpty()) arp_resetCounter(settings.syncMode==smInternal);

		// assign note			
		
		if(arp.mode!=amUpDown)
		{
			for(i=0;i<ARP_NOTE_MEMORY;++i)
				if(arp.notes[i]==ASSIGNER_NO_NOTE)
				{
					arp.notes[i]=note;
					break;
				}
		}
		else
		{
			arp.notes[note]=note;
			arp.notes[ARP_LAST_NOTE-note]=note;
		}
	}
	else
	{
		if(arp.hold)
		{
			// mark deassigned notes as held

			if(arp.mode!=amUpDown)
			{
				for(i=0;i<ARP_NOTE_MEMORY;++i)
					if(arp.notes[i]==note)
					{
						arp.notes[i]|=ARP_NOTE_HELD_FLAG;
						break;
					}
			}
			else
			{
				arp.notes[note]|=ARP_NOTE_HELD_FLAG;
				arp.notes[ARP_LAST_NOTE-note]|=ARP_NOTE_HELD_FLAG;
			}
		}
		else
		{
			// deassign note if not in hold mode

			if(arp.mode!=amUpDown)
			{
				for(i=0;i<ARP_NOTE_MEMORY;++i)
					if(arp.notes[i]==note)
					{
						arp.notes[i]=ASSIGNER_NO_NOTE;
						break;
					}
			}
			else
			{
				arp.notes[note]=ASSIGNER_NO_NOTE;
				arp.notes[ARP_LAST_NOTE-note]=ASSIGNER_NO_NOTE;
			}

			// gate off for last note

			if(isEmpty())
				finishPreviousNote();
		}
	}
}

void arp_update(void)
{
	uint8_t n, step;
	
	// arp off -> nothing to do
	
	if(arp.mode==amOff)
		return;
	
	// nothing to play ?
	
	if(isEmpty())
		return;
	
	// yep
	
	finishPreviousNote();
			
	// act depending on mode
	
	switch(arp.mode)
	{
        case amUpDown:
            do
                arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY; // cycle through the array
            while (arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE || (arp.previousIndex==(ARP_LAST_NOTE-arp.noteIndex))); // add here the avoidance of top/bottom note double playing
            break;
        case amAssign:
            do
                arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY;
            while(arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE);
            break;

        case amRandom:
            step=0;
            for (n=0;n<ARP_NOTE_MEMORY;n++)
            {
                if (arp.notes[n]!=ASSIGNER_NO_NOTE) step++;
            }
            // n is the random number of notes that will be skipped. Step is always >0 if there is more than one note but alsoways =0 if there is exactly one note
            n=0;
            if (step>1)
			{
				n=(random()%(step-1))+1;
			}
			if (arp.noteIndex<0) arp.noteIndex=0;
            while(arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE || n>0)
            {
                arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY;
                if (arp.notes[arp.noteIndex]!=ASSIGNER_NO_NOTE) n--;
            }
            break;
        default:
            return;
	}
	
	n=arp.notes[arp.noteIndex]&~ARP_NOTE_HELD_FLAG;
	
	// send note to assigner, velocity at half (MIDI value 64)
	
	assigner_assignNote(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE,0);
	
	// pass to MIDI out only in local on mode:
    // in local off mode external MIDI plays into arp so if MIDI out is connected (via MIDI recording tools) to MIDI in we would get an infinite loop...
    if (settings.midiMode==0)
    {
        midi_sendNoteEvent(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE);
    }

	arp.previousNote=arp.notes[arp.noteIndex];
	arp.previousTranspose=arp.transpose;
	arp.previousIndex=arp.noteIndex;
}

void arp_init(void)
{
	memset(&arp,0,sizeof(arp));

	memset(arp.notes,ASSIGNER_NO_NOTE,ARP_NOTE_MEMORY);
	
	arp.noteIndex=-1;
	arp.previousNote=ASSIGNER_NO_NOTE;
}
