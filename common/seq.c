////////////////////////////////////////////////////////////////////////////////
// Polyphonic multitrack step sequencer
////////////////////////////////////////////////////////////////////////////////

#include "seq.h"

#include "scanner.h"
#include "assigner.h"
#include "storage.h"
#include "midi.h"
#include "clock.h"
#include "arp.h"

struct track
{
	seqMode_t mode;
	uint8_t events[SEQ_NOTE_MEMORY];
	uint8_t eventCount;
	int16_t eventIndex;
	int16_t prevEventIndex;
	uint8_t stepCount;
	int8_t previousTranspose;
};

static struct
{
	struct track tracks[SEQ_TRACK_COUNT];

	int8_t transpose;
	// During note entry:
	uint8_t addTies; // how many ties to add
	uint8_t noteOns; // how many keys are down
} seq;


static int8_t anyTrackPlaying(void)
{
	int8_t track;

	for (track = 0; track < SEQ_TRACK_COUNT; track++)
		if (seq.tracks[track].mode==smPlaying)
			return 1;
	return 0;
}

static void finishPreviousNotes(struct track *tp)
{	
	uint8_t s,n;

	if(!tp->eventCount||tp->prevEventIndex<0) // return if no events or no previous event
		return;

	s=tp->events[tp->prevEventIndex];
	do {
		s&=SEQ_NOTEBITS;

		if(s==SEQ_REST||s==SEQ_TIE)
			break;

		// handle notes
		n=s+SCANNER_BASE_NOTE+tp->previousTranspose;

		// send note to assigner, velocity at half (MIDI value 64)
		// is it ok to always send that, even in local off mode  what's the side effect?
		assigner_assignNote(n,0,0,0);

		// pass to MIDI out but not in local off mode
		if (settings.midiMode==0) midi_sendNoteEvent(n,0,0);

		tp->prevEventIndex=(tp->prevEventIndex+1)%tp->eventCount;
		s=tp->events[tp->prevEventIndex];
	} while(s&SEQ_CONT);
}

static FORCEINLINE void playStep(int8_t track)
{
	uint8_t s,n;
	struct track *tp = &seq.tracks[track];

	// seq not playing -> nothing to do

	if(tp->mode!=smPlaying)
		return;

	// nothing to play ?

	if(!tp->eventCount)
		return;

	s=tp->events[tp->eventIndex];
	if(s!=SEQ_TIE) // terminate previous unless it's a tie
		finishPreviousNotes(tp);
	if(s!=SEQ_REST&&s!=SEQ_TIE) // a note
	{
		// save note index so we can do note off later
		tp->prevEventIndex=tp->eventIndex;
		tp->previousTranspose=seq.transpose;
	}
	do {
		s&=SEQ_NOTEBITS;
		if(s!=SEQ_REST&&s!=SEQ_TIE) // a note
		{	
			// handle notes
			n=s+SCANNER_BASE_NOTE+seq.transpose;

			// send note to assigner, velocity at half (MIDI value 64)
            assigner_assignNote(n,1,HALF_RANGE,0);

			// pass to MIDI out but not in local off mode
			if (settings.midiMode==0) midi_sendNoteEvent(n,1,HALF_RANGE);

		}
		tp->eventIndex=(tp->eventIndex+1)%tp->eventCount; // this cycles through the number of events by mod(counter)
		s=tp->events[tp->eventIndex];
	} while(s&SEQ_CONT); // all notes with this bit set are part of the same "chord", e.g. which ae the continuation flag set 
}

inline void seq_setMode(int8_t track, seqMode_t mode)
{
	struct track *tp = &seq.tracks[track];
	int8_t alreadyPlaying;

	seqMode_t oldMode=tp->mode;

	if(mode==oldMode)
		return; // this function only deals with sequencer mode changes

	alreadyPlaying=anyTrackPlaying();

	if(oldMode==smOff)
	{
		// load sequence from storage on start
		if(!storage_loadSequencer(track,tp->events,SEQ_NOTE_MEMORY))
			memset(tp->events,ASSIGNER_NO_NOTE,SEQ_NOTE_MEMORY);

		// compute note and step count
		tp->eventCount=0;
		tp->stepCount=0;
		while(tp->eventCount<SEQ_NOTE_MEMORY)
		{
			uint8_t s=tp->events[tp->eventCount];
			if(!(s&SEQ_CONT))
				tp->stepCount++;
			if(s==ASSIGNER_NO_NOTE)
				break;
			tp->eventCount++;
		}
	}
	else if(oldMode==smRecording)
	{
		// store sequence to storage on record end
		storage_saveSequencer(track,tp->events,SEQ_NOTE_MEMORY);
	}
	else if(oldMode==smPlaying)
	{
		finishPreviousNotes(tp);
	}	

	if(mode==smPlaying)
		seq_resetCounter(track,settings.syncMode==smInternal);

	if(mode==smRecording)
		seq.addTies=0;

	tp->mode=mode;

	// We need to put this after setting tp->mode to play, or playStep 
	// won't play anything.
	// The /2 bit is to determine if the second sequence has been
	// started just before or just after a step has been played of
	// the first. If seq.counter is closer to 0 than to seq.speed,
	// then the second sequence was started just after the first had
	// played its step, so we play the first step of the second sequence
	// as fast as we can so it is heard (almost) simultaneously with
	// the step of the first sequence. Conversely, if seq.counter is closer
	// to seq.speed, the second sequence was started slightly before
	// the first had played its step (this only happens when the second
	// sequence is started after the first has already played (at least)
	// one step), so we don't play the step here, but let it be played
	// as usual from seq_update().
	uint16_t speed=clock_getSpeed();
	if(mode==smPlaying&&alreadyPlaying&&speed!=UINT16_MAX&&clock_getCounter()<speed/2)
		playStep(track);
}

FORCEINLINE void seq_setTranspose(int8_t transpose)
{
	seq.transpose=transpose;
}

FORCEINLINE void seq_silence(int8_t track)
{
	if(seq.tracks[track].mode==smPlaying)
		finishPreviousNotes(&seq.tracks[track]);
}

FORCEINLINE void seq_resetCounter(int8_t track, int8_t beatReset)
{
	seq_silence(track);
	seq.tracks[track].eventIndex=0; // reinit
	seq.tracks[track].prevEventIndex=-1;
	if (!anyTrackPlaying()&&arp_getMode()==amOff) // it's a fresh start
	{ 
		synth_resetClockBar(); // reset the LFO sync counter
		if(beatReset) // the sync is to internal, so we should rest the clock
			clock_reset(); // start immediately
	}
}

FORCEINLINE seqMode_t seq_getMode(int8_t track)
{
	return seq.tracks[track].mode;
}

FORCEINLINE uint8_t seq_getStepCount(int8_t track)
{
	return seq.tracks[track].stepCount;
}

FORCEINLINE int8_t seq_full(int8_t track)
{
	return seq.tracks[track].eventCount+seq.addTies>=SEQ_NOTE_MEMORY;
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

static int8_t spaceAvail(struct track *tp)
{
  // We need to have space not only for notes but also for any added
  // tie events.
  return tp->eventCount+seq.addTies<SEQ_NOTE_MEMORY;
}

static FORCEINLINE void inputNote(struct track *tp, uint8_t note, uint8_t pressed)
{
	if(tp->mode!=smRecording)
		return;

	if(note==SEQ_NOTE_CLEAR) // this 'note' resets the entire track
	{
		tp->eventCount=0;
		tp->stepCount=0;
		seq.addTies=0;
		memset(tp->events,ASSIGNER_NO_NOTE,SEQ_NOTE_MEMORY);
		return;
	}

	if(note==SEQ_NOTE_UNDO)
	{
		if(!tp->stepCount) // break if no events in sequence
			return;
		tp->stepCount--; // back up one step
		if (seq.addTies) // currently entering tie
		{
			seq.addTies--;
			return;
		}
		// erase all events which belong to the same SEQ_CONT block back to previous one
		while(tp->eventCount)
		{
			uint8_t s=tp->events[--tp->eventCount];
			tp->events[tp->eventCount]=ASSIGNER_NO_NOTE;
			if(!(s&SEQ_CONT))
				break;
		}
		return;
	}

	if(note==SEQ_NOTE_STEP)
	{
		// If there are no events, simply insert a rest event..
		// Otherwise we bump the #ties counter.
		if(!spaceAvail(tp))
			return;
		tp->stepCount++;
		if (!seq.noteOns) // no notes down => add rest
		{
			tp->events[tp->eventCount]=SEQ_REST;
			tp->eventCount++;
		}
		else // just count tie events to be added later
			seq.addTies++;
		return;
	}

	// ordinary note on/off
	if (pressed)
	{
		int8_t first=!seq.noteOns; // first of chord
		seq.noteOns++;
		if(!spaceAvail(tp))
			return;
		note-=SCANNER_BASE_NOTE;
		// Advance step count when we hit first note of a chord.
		if (first)
			tp->stepCount++;
		else
		{
			// check for duplicates
			int8_t searchIndex=tp->eventCount-1;
			uint8_t event;
			do {
				event=tp->events[searchIndex];
				if((event&SEQ_NOTEBITS)==note)
					return; // duplicate, so don't use
			} while((event&SEQ_CONT)&&--searchIndex>=0);
		}
		tp->events[tp->eventCount++]=note|(first?0:SEQ_CONT);
	}
	else
	{
		if(seq.noteOns)
			seq.noteOns--;
		if(seq.noteOns) // still notes down
			return;
		// When last note in chord released,
		// put down additional tie events
		// We know there's space for these, as spaceAvail()
		// during note entry takes it into account
		memset(&tp->events[tp->eventCount],SEQ_TIE,seq.addTies);
		tp->eventCount+=seq.addTies;
		seq.addTies=0;
	}
}

void seq_inputNote(uint8_t note, uint8_t pressed)
{
	for(int8_t track=0;track<SEQ_TRACK_COUNT;++track)
		inputNote(&seq.tracks[track], note, pressed);
}

void seq_update(void)
{
	for(int8_t track=0;track<SEQ_TRACK_COUNT;++track)
		playStep(track);
}

void seq_init(void)
{
	int8_t track;

	memset(&seq,0,sizeof(seq));

	for(track=0;track<SEQ_TRACK_COUNT;++track)
	{
		seq.tracks[track].eventIndex=0;
		memset(seq.tracks[track].events,ASSIGNER_NO_NOTE,SEQ_NOTE_MEMORY);
	}		
}
