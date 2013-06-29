////////////////////////////////////////////////////////////////////////////////
// Arpeggiator
////////////////////////////////////////////////////////////////////////////////

#include "arp.h"

#include "assigner.h"

#define ARP_NOTE_MEMORY 128 // must stay>=128 for up/down mode

#define ARP_LAST_NOTE (ARP_NOTE_MEMORY-1)

static struct
{
	uint8_t notes[ARP_NOTE_MEMORY];
	int16_t noteIndex;
	int8_t isDown;

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

static void killAllNotes(void)
{
	int16_t i;

	for(i=0;i<ARP_NOTE_MEMORY;++i)
		arp.notes[i]=ASSIGNER_NO_NOTE;

	arp.noteIndex=-1;
	
	assigner_voiceDone(-1);
}

inline void arp_setMode(arpMode_t mode, int8_t hold)
{
	// stop previous assigned notes
	
	if(arp.mode!=mode || !hold)
		killAllNotes();
	
	arp.mode=mode;
	arp.hold=hold;
}

inline void arp_setSpeed(uint16_t speed)
{
	arp.speed=16+((UINT16_MAX-speed)>>7);
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
	
	if(on)
	{
		// if this is the first note, make sure the arp will start on it as as soon as we update
		
		if(isEmpty())
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
			killAllNotes();
	}
}

void arp_init(void)
{
	int16_t i;
	
	memset(&arp,0,sizeof(arp));

	for(i=0;i<ARP_NOTE_MEMORY;++i)
		arp.notes[i]=ASSIGNER_NO_NOTE;
	
	arp.noteIndex=-1;
}

void arp_update(void)
{
	// arp off -> nothing to do
	
	if(arp.mode==amOff)
		return;
	
	// speed management
	
	++arp.counter;
	
	if(arp.counter<arp.speed)
		return;
	
	arp.counter=0;
	
	// nothing to play ?
	
	if(isEmpty())
		return;
	
	// act depending on mode
	
	uint8_t note;
	
	switch(arp.mode)
	{
	case amUpDown:
	case amAssign:
		do
			arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY;
		while(arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE);
		
		note=arp.notes[arp.noteIndex];
		break;
		
	case amRandom:
		do
			arp.noteIndex=random()%ARP_NOTE_MEMORY;
		while(arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE);

		note=arp.notes[arp.noteIndex];
		break;
	default:
		return;
	}
	
	// send note to assigner
	
	assigner_assignNote(note,1,UINT16_MAX,0);
}

