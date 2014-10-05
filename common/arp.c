////////////////////////////////////////////////////////////////////////////////
// Arpeggiator
////////////////////////////////////////////////////////////////////////////////

#include "arp.h"

#include "scanner.h"
#include "assigner.h"
#include "storage.h"
#include "midi.h"

#define ARP_NOTE_MEMORY 128 // must stay>=128 for up/down mode

#define ARP_NOTE_HELD_FLAG 0x80

#define ARP_LAST_NOTE (ARP_NOTE_MEMORY-1)

const uint16_t extClockDividers[18] = {384,192,168,144,128,96,72,48,36,24,18,12,9,6,4,3,2,1};

static struct
{
	uint8_t notes[ARP_NOTE_MEMORY];
	int16_t noteIndex;
	uint8_t previousNote;
	int8_t transpose,previousTranspose;

	uint16_t counter,speed;
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
		
		assigner_assignNote(n+SCANNER_BASE_NOTE+arp.previousTranspose,0,0);
		
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
	assigner_voiceDone(-1);
}

static void markNotesAsHeld(void)
{
	int16_t i;
	for(i=0;i<ARP_NOTE_MEMORY;++i)
		arp.notes[i]|=ARP_NOTE_HELD_FLAG;
}

static void killHeldNotes(void)
{
	int16_t i;
	for(i=0;i<ARP_NOTE_MEMORY;++i)
		if(arp.notes[i]&ARP_NOTE_HELD_FLAG)
			arp.notes[i]=ASSIGNER_NO_NOTE;
}

inline void arp_setMode(arpMode_t mode, int8_t hold)
{
	// stop previous assigned notes
	
	if(mode!=arp.mode)
	{
		killAllNotes();
		
		if(settings.syncMode!=smInternal)
			arp_resetCounter();
	}
	
	if(hold!=arp.hold)
	{
		if(hold)
			markNotesAsHeld();
		else
			killHeldNotes();
	}

	arp.mode=mode;
	arp.hold=hold;
}

inline void arp_setSpeed(uint16_t speed)
{
	if(speed<1024)
		arp.speed=UINT16_MAX;
	else if(settings.syncMode==smInternal)
		arp.speed=exponentialCourse(speed,22000.0f,500.0f);
	else
		arp.speed=extClockDividers[((uint32_t)speed*(sizeof(extClockDividers)/sizeof(uint16_t)))>>16];
}

void arp_setTranspose(int8_t transpose)
{
	arp.transpose=transpose;
}

void arp_resetCounter(void)
{
	arp.counter=INT16_MAX; // start on a note
}

inline arpMode_t arp_getMode(void)
{
	return arp.mode;
}

int8_t arp_getHold(void)
{
	return arp.hold;
}

void arp_assignNote(uint8_t note, int8_t on)
{
	int16_t i;
	
	// We only arpeggiate from the internal keyboard, so we can keep the
	// note memory size at 128 if we set the keyboard range to 0 and up.
	note-=SCANNER_BASE_NOTE;
	if(on)
	{
		// if this is the first note, make sure the arp will start on it as as soon as we update
		
		if(isEmpty() && settings.syncMode==smInternal)
			arp.counter=INT16_MAX; // not UINT16_MAX, to avoid overflow

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
		
		if(arp.hold==1)
			markNotesAsHeld();
	}
	else if(!arp.hold)
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

void arp_update(void)
{
	uint8_t n;
	
	// arp off -> nothing to do
	
	if(arp.mode==amOff)
		return;
	
	// speed management
	
	if(arp.speed==UINT16_MAX)
		return;

	++arp.counter;
	
	if(arp.counter<arp.speed)
		return;
	
	arp.counter=0;
	
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
	
	assigner_assignNote(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE);
	
	// pass to MIDI out

	midi_sendNoteEvent(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE);

	arp.previousNote=arp.notes[arp.noteIndex];
	arp.previousTranspose=arp.transpose;
}

void arp_init(void)
{
	int16_t i;
	
	memset(&arp,0,sizeof(arp));

	for(i=0;i<ARP_NOTE_MEMORY;++i)
		arp.notes[i]=ASSIGNER_NO_NOTE;
	
	arp.noteIndex=-1;
	arp.previousNote=ASSIGNER_NO_NOTE;
}
