////////////////////////////////////////////////////////////////////////////////
// Multitack step sequencer
////////////////////////////////////////////////////////////////////////////////

#include "seq.h"

#include "scanner.h"
#include "assigner.h"
#include "storage.h"
#include "midi.h"

static struct
{
	seqMode_t mode[SEQ_TRACK_COUNT];
	uint8_t events[SEQ_TRACK_COUNT][SEQ_NOTE_MEMORY];
	uint8_t eventCount[SEQ_TRACK_COUNT];
	int16_t eventIndex[SEQ_TRACK_COUNT];
	int16_t prevEventIndex[SEQ_TRACK_COUNT];
	uint8_t stepCount[SEQ_TRACK_COUNT];

	int8_t transpose,previousTranspose[SEQ_TRACK_COUNT];
	uint16_t counter,speed;

	uint8_t addTies;
	uint8_t noteOns; /* how many keys are down */
} seq;


static void finishPreviousNotes(int8_t track)
{	
	uint8_t s,n;

	if(!seq.eventCount[track]||seq.prevEventIndex[track]<0)
		return;

	s=seq.events[track][seq.prevEventIndex[track]];
	do {
		s&=SEQ_NOTEBITS;

		if(s==SEQ_REST||s==SEQ_TIE)
			break;

		// handle notes
		n=s+SCANNER_BASE_NOTE+seq.previousTranspose[track];

		// send note to assigner, velocity at half (MIDI value 64)
		assigner_assignNote(n,0,0);

		// pass to MIDI out
		midi_sendNoteEvent(n,0,0);

		seq.prevEventIndex[track]=(seq.prevEventIndex[track]+1)%seq.eventCount[track];
		s=seq.events[track][seq.prevEventIndex[track]];
	} while(s&SEQ_CONT);
}

inline void seq_setMode(int8_t track, seqMode_t mode)
{
	seqMode_t oldMode=seq.mode[track];

	if(mode==oldMode)
		return;

	if(oldMode==smOff)
	{
		// load sequence from storage on start
		if(!storage_loadSequencer(track,seq.events[track],SEQ_NOTE_MEMORY))
			memset(seq.events[track],ASSIGNER_NO_NOTE,SEQ_NOTE_MEMORY);

		// compute note and step count
		seq.eventCount[track]=0;
		seq.stepCount[track]=0;
		while(seq.eventCount[track]<SEQ_NOTE_MEMORY)
		{
			uint8_t s=seq.events[track][seq.eventCount[track]];
			if(!(s&SEQ_CONT))
				seq.stepCount[track]++;
			if(s==ASSIGNER_NO_NOTE)
				break;
			seq.eventCount[track]++;
		}
	}
	else if(oldMode==smRecording)
	{
		// store sequence to storage on record end
		storage_saveSequencer(track,seq.events[track],SEQ_NOTE_MEMORY);
	}
	else if(oldMode==smPlaying)
	{
		finishPreviousNotes(track);
	}	

	if(mode==smPlaying)
		seq_resetCounter(track);

	if(mode==smRecording)
		seq.addTies=0;

	seq.mode[track]=mode;
}

inline void seq_setSpeed(uint16_t speed)
{
	if(speed<1024)
		seq.speed=UINT16_MAX;
	else if(settings.syncMode==smInternal)
		seq.speed=exponentialCourse(speed,22000.0f,500.0f);
	else
		seq.speed=extClockDividers[((uint32_t)speed*(sizeof(extClockDividers)/sizeof(uint16_t)))>>16];
}

FORCEINLINE void seq_setTranspose(int8_t transpose)
{
	seq.transpose=transpose;
}

FORCEINLINE void seq_resetCounter(int8_t track)
{
	seq.eventIndex[track]=0; // reinit
	seq.counter=INT16_MAX; // start on a note
	seq.prevEventIndex[track]=-1;
}

void seq_silence(int8_t track)
{
	if(seq.mode[track]==smPlaying)
		finishPreviousNotes(track);
}

FORCEINLINE seqMode_t seq_getMode(int8_t track)
{
	return seq.mode[track];
}

FORCEINLINE uint8_t seq_getStepCount(int8_t track)
{
	return seq.stepCount[track];
}

FORCEINLINE int8_t seq_full(int8_t track)
{
	return seq.eventCount[track]+seq.addTies>=SEQ_NOTE_MEMORY;
}

static FORCEINLINE void noteOnCount(void)
{
	seq.noteOns++;
}

static FORCEINLINE void noteOffCount(void)
{
	if (seq.noteOns) // should not be needed, but for robustness
		seq.noteOns--;
}

static int8_t spaceAvail(int8_t track)
{
  // We need to have space not only for notes but also for any added
  // tie events.
  return seq.eventCount[track]+seq.addTies<SEQ_NOTE_MEMORY;
}

void seq_inputNote(uint8_t note, uint8_t pressed)
{
	for(int8_t track=0;track<SEQ_TRACK_COUNT;++track)
	{
		if(seq.mode[track]!=smRecording)
			continue;

		if(note==SEQ_NOTE_CLEAR)
		{
			seq.eventCount[track]=0;
			seq.stepCount[track]=0;
			seq.addTies=0;
			memset(seq.events[track],ASSIGNER_NO_NOTE,SEQ_NOTE_MEMORY);
		}
		else if(note==SEQ_NOTE_UNDO)
		{
			if(!seq.stepCount[track]) /* break if no events */
				continue;
			seq.stepCount[track]--; /* back up one step */
			if (seq.addTies) /* currently entering tie */
			{
				seq.addTies--;
				continue;
			}
			if (seq.noteOns) /* any notes down? */
			{
				/* erase from entry buffer */
				while (seq.eventCount[track])
					seq.events[track][--seq.eventCount[track]]=ASSIGNER_NO_NOTE;
				seq.noteOns=0; /* a bit of a kludge */
				/* but since we've deleted the current entry,
				 * for all practical purposes there are no
				 * keys down now. */
				continue;
			}
			/* erase all events back to previous one */
			while(seq.eventCount[track])
			{
				uint8_t s=seq.events[track][--seq.eventCount[track]];
				seq.events[track][seq.eventCount[track]]=ASSIGNER_NO_NOTE;
				if(!(s&SEQ_CONT))
					break;
			}
		}
		else if(note==SEQ_NOTE_STEP)
		{
			/* If there are no events, simply insert a rest event.
			 * Otherwise we bump the #ties counter . */
			/* TODO: use bit 6 for rest events, with lower bits
			 * a counter? In that case we insert one if there
			 * isn't one already, otherwise bump it. */
			if(!spaceAvail(track))
				continue;
			seq.stepCount[track]++;
			if (!seq.noteOns) // no notes down => add rest
			{
				seq.events[track][seq.eventCount[track]]=SEQ_REST;
				seq.eventCount[track]++;
			}
			else // just count tie events to be added later
				seq.addTies++;
		}
		else
		{
			if (pressed) {
				int8_t first=!seq.noteOns; /* first of chord*/
				seq.noteOns++;
				/* TODO: We need to check if the note already
				 * exists in this event; if so, it means it
				 * released and pressed again, and we don't
				 * need to / shouldn't add it. */
				if(spaceAvail(track))
				{
					seq.events[track][seq.eventCount[track]++]=(note-SCANNER_BASE_NOTE)|(first?0:SEQ_CONT);
					if (first)
						seq.stepCount[track]++;
				}
			}
			else
			{
				if (seq.noteOns)
					seq.noteOns--;
				if(!seq.noteOns) /* last note released */
				{
					// additional tie events
					// We know there's space for these,
					// as spaceAvail() takes it into account
					/* TODO: use bit 6 for counter,
					 * thus we just insert one event for
					 * up to 64 extra steps? */
					memset(&seq.events[track][seq.eventCount[track]],SEQ_TIE,seq.addTies);
					seq.eventCount[track]+=seq.addTies;
					seq.addTies=0;
					/* seq.addTies is now 0 */
				}
			}
		}
	}
}

static FORCEINLINE void playStep(int8_t track)
{
	uint8_t s,n;

	s=seq.events[track][seq.eventIndex[track]];
	if(s!=SEQ_TIE) // terminate previous unless it's a tie
		finishPreviousNotes(track);
	if(s!=SEQ_REST&&s!=SEQ_TIE) /* a note */
	{
		// save note index so we can do note off later
		seq.prevEventIndex[track]=seq.eventIndex[track];
		seq.previousTranspose[track]=seq.transpose;
	}
	do {
		s&=SEQ_NOTEBITS;
		if(s!=SEQ_REST&&s!=SEQ_TIE) /* a note */
		{	
			// handle notes
			n=s+SCANNER_BASE_NOTE+seq.transpose;

			// send note to assigner, velocity at half (MIDI value 64)
			assigner_assignNote(n,1,HALF_RANGE);

			// pass to MIDI out
			midi_sendNoteEvent(n,1,HALF_RANGE);

		}
		seq.eventIndex[track]=(seq.eventIndex[track]+1)%seq.eventCount[track];
		s=seq.events[track][seq.eventIndex[track]];
	} while(s&SEQ_CONT);
}

void seq_update(void)
{
	// speed management

	if(seq.speed==UINT16_MAX)
		return;

	++seq.counter;

	if(seq.counter<seq.speed)
		return;

	seq.counter=0;

	for(int8_t track=0;track<SEQ_TRACK_COUNT;++track)
	{
		// seq not playing -> nothing to do

		if(seq.mode[track]!=smPlaying)
			continue;

		// nothing to play ?

		if(!seq.eventCount[track])
			continue;

		playStep(track);
	}
}

void seq_init(void)
{
	int8_t track;

	memset(&seq,0,sizeof(seq));

	for(track=0;track<SEQ_TRACK_COUNT;++track)
	{
		seq.eventIndex[track]=0;
		memset(seq.events[track],ASSIGNER_NO_NOTE,SEQ_NOTE_MEMORY);
	}		
}
