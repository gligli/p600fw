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

#define ARP_NOTE_HELD_FLAG 0x80

#define ARP_LAST_NOTE (ARP_NOTE_MEMORY-1)

static struct
{
	uint8_t notes[ARP_NOTE_MEMORY];
	int16_t noteIndex;
	int16_t previousIndex;
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
		uint8_t n=arp.previousNote&~ARP_NOTE_HELD_FLAG;
		
		assigner_assignNote(n+SCANNER_BASE_NOTE+arp.previousTranspose,0,0,0);
		
		// pass to MIDI out
		midi_sendNoteEvent(n+SCANNER_BASE_NOTE+arp.previousTranspose,0,0);
	}
}

static void killAllNotes(void)
{
	finishPreviousNote();

	arp.noteIndex=-1;
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
	{
		finishPreviousNote();
	}
}

inline void arp_setMode(arpMode_t mode, int8_t hold)
{
	// stop previous assigned notes
	
	if(mode!=arp.mode)
	{
		killAllNotes();
		
		if (mode!=amOff)
			arp_resetCounter(settings.syncMode==smInternal);
	}
	
	if(!hold && arp.hold)
		killHeldNotes();

	arp.mode=mode;
	arp.hold=hold;
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
		
		if(isEmpty())
			arp_resetCounter(settings.syncMode==smInternal);

		// assign note			
		
		if(arp.mode!=amUpDown)
		{
			for(i=0;i<ARP_NOTE_MEMORY;++i)
				if(arp.notes[i]==ASSIGNER_NO_NOTE)
				{
					arp.notes[i]=note; // plase the note on the first "empty" place
					break;
				}
		}
		else
		{
			// up-down construction
			arp.notes[note]=note; // place the note on the place corresponding to the index of the note (up part) 
			arp.notes[ARP_LAST_NOTE-note]=note; // place the note also on the notes place from the top (down part)
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
						arp.notes[i]|=ARP_NOTE_HELD_FLAG; // assumes that the note can only be in one place
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
	uint8_t n;
	
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
	case amAssign:
		do
			arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY;
		while(arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE);
		break;
		
	case amRandom:
		do
			arp.noteIndex=random()%ARP_NOTE_MEMORY;
		while(arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE);
		break;
	default:
		return;
	}
	
	n=arp.notes[arp.noteIndex]&~ARP_NOTE_HELD_FLAG;
	
	// send note to assigner, velocity at half (MIDI value 64)
	
	assigner_assignNote(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE,0);
	
	// pass to MIDI out

	midi_sendNoteEvent(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE);

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
