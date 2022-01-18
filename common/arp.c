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

#define ARP_NOTE_MEMORY 128 // this is the current maximum. For up/down the number should not be less than 61

// this flag is used to indicate which of the current arp notes are latched so that these can be
// removed when latch mode is deactivated while the currently held keys continue to be played:
#define ARP_NOTE_HELD_FLAG 0x80 

static struct
{
	uint8_t notes[ARP_NOTE_MEMORY];
	int16_t noteIndex;
	int16_t previousIndex; // this is used to avoid muliple successive playing of notes (or indices) in random arp
	uint8_t previousNote;
	int8_t transpose;
	int8_t previousTranspose;
	int8_t numberOfNotes; // only used for the random mode to reduce sampling effort

	int8_t hold;
	arpMode_t mode;
} arp;

static int8_t isEmpty(void)
{
	int16_t i;
	
	for(i=0;i<ARP_NOTE_MEMORY;++i)
		if(arp.notes[i]!=ASSIGNER_NO_NOTE)
			return 0;

	arp.numberOfNotes = 0;
	return 1;
}


static void finishPreviousNote(void)
{
	if(arp.previousNote!=ASSIGNER_NO_NOTE)
	{
		uint8_t n=arp.previousNote&~ARP_NOTE_HELD_FLAG; // remove the HELD bit

		// is it ok to send this event though it should be off in local off mode? What's the side effect?
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
	arp.previousIndex=-1;
	arp.numberOfNotes=0;
	memset(arp.notes,ASSIGNER_NO_NOTE,ARP_NOTE_MEMORY);
	assigner_allKeysOff();
}

static void killHeldNotes(void)
{

	// to remove all the held notes we need to reorder...
	int16_t i, j;
	j=0;
	for(i=0;i<ARP_NOTE_MEMORY;++i)
	{
		if(!(arp.notes[i]&ARP_NOTE_HELD_FLAG))
		{
			if (j!=i) // carry this note over
			{
				arp.notes[j]=arp.notes[i]; 
			}
			j++;
		}
	}

	// reset
	arp.noteIndex=-1;
	arp.numberOfNotes=j;
	
	// now overwrite the remaining slots...
	for(i=j;i<ARP_NOTE_MEMORY;++i)
	{
		arp.notes[i]=ASSIGNER_NO_NOTE;
	}
	
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
	arp.hold=mode==amOff?0:hold;
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
	int16_t i, findIndex;
	
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

		if(arp.mode!=amUpDown) // assign or random mode
		{
			if(arp.numberOfNotes < ARP_NOTE_MEMORY) // only if note can be added
			{
				// we know that last added note is at index numberOfNotes-1
				arp.notes[arp.numberOfNotes]=note;
				arp.numberOfNotes++;
			}		
		}
		else // up/down mode
		{
            arp.notes[note]=note;
            arp.notes[ARP_NOTE_HELD_FLAG-note]=note;
            arp.numberOfNotes+=2;
		}
	}
	else
	{

		// note: the conditions should in fact be equivalent...
		if(isEmpty() || arp.numberOfNotes==0) // this can happen if you enter into arp mode with notes held and then released, in this case do nothing
			return;

		// from this point on arp.numberOfNotes can only be >= 1
		
		if(arp.hold)
		{
			// mark deassigned note as held
			// for assign/random this will be the latest matching note
			// for up/down there can only be one anyway
			// to use just one routine for all cases, start from top...
			if (arp.mode!=amUpDown)
            {
                for(i=arp.numberOfNotes-1;i>=0;--i)
                {
                    if(arp.notes[i]==note)
                    {
                        arp.notes[i]|=ARP_NOTE_HELD_FLAG;
                        break;
                    }
                }
            }
            else // up/down
            {
                arp.notes[note]|=ARP_NOTE_HELD_FLAG;
                arp.notes[ARP_NOTE_HELD_FLAG-note]|=ARP_NOTE_HELD_FLAG;
            }
		}
		else
		{

			if (arp.mode!=amUpDown)
            {
                // we don't want to leave "holes", therefore rearrange, start from top
                // note: the first note we find and if we find it CANNOT be held because
                // either the key was pressed before entering the arp mode in which case notes[] is empty
                // or the note has been played in arp mode and not yet release in which case the note is not yet held
                // so we remove it on key off...
                findIndex = arp.numberOfNotes-1;
                while (findIndex>=0)
                {
                    if (arp.notes[findIndex]==note) // this can only match notes wich are not held
                    {
                        break;
                    }
                    findIndex--;
                }

                if (findIndex<0) // note not found, abort without action
                    return;

                // findIndex is the index of the matching note
                // now shift the higher notes down
                for (i=findIndex;i<arp.numberOfNotes-1;++i)
                {
                    arp.notes[i]=arp.notes[i+1];
                }
                arp.notes[arp.numberOfNotes-1]=ASSIGNER_NO_NOTE;
                // adjust the index for the running arp if it happens to point to the previously last note
                // otherwise the up/down arp runs away...
                if (arp.noteIndex==arp.numberOfNotes-1)
                {
                    arp.noteIndex=arp.numberOfNotes-2;
                    arp.previousIndex=arp.numberOfNotes-2;
                }
                arp.numberOfNotes--;
            }
            else
            {
                arp.notes[note]=ASSIGNER_NO_NOTE;
                arp.notes[ARP_NOTE_HELD_FLAG-note]=ASSIGNER_NO_NOTE;
                arp.numberOfNotes-=2;
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
		if (arp.numberOfNotes>1)
		{
            // add here the avoidance of top/bottom note double playing
            do
                arp.noteIndex=(arp.noteIndex+1)%ARP_NOTE_MEMORY; // cycle through the array
            while (arp.notes[arp.noteIndex]==ASSIGNER_NO_NOTE || arp.noteIndex==ARP_NOTE_HELD_FLAG-arp.previousIndex);
		}
		else
		{
			arp.noteIndex=0;
		}
		break;
	case amAssign:
		arp.noteIndex=(arp.noteIndex+1)%arp.numberOfNotes; 
		break;
	case amRandom:
		if (arp.numberOfNotes>1)
		{
			do
				arp.noteIndex=random()%arp.numberOfNotes;
			while(arp.noteIndex==arp.previousIndex); 
		}
		else
		{
			arp.noteIndex=0;
		}				
		break;
	default:
		return;
	}

	n=arp.notes[arp.noteIndex]&~ARP_NOTE_HELD_FLAG; // remove the HELD bit

	if(arp.notes[arp.noteIndex]!=ASSIGNER_NO_NOTE)
	{
		// send note to assigner, velocity at half (MIDI value 64)
		if (settings.midiMode==0) // only play in local on mode
			assigner_assignNote(n+SCANNER_BASE_NOTE+arp.transpose,1,HALF_RANGE,0);
		
		// pass to MIDI out

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
	arp.numberOfNotes =0;
	
}
