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
	uint8_t notes[SEQ_TRACK_COUNT][SEQ_NOTE_MEMORY];
	uint8_t noteCount[SEQ_TRACK_COUNT];
	int16_t noteIndex[SEQ_TRACK_COUNT];

	int8_t transpose,previousTranspose[SEQ_TRACK_COUNT];
	uint16_t counter,speed;
} seq;


static void finishPreviousNote(int8_t track, int8_t offset)
{	
	uint8_t n;
	int16_t ni;
	
	if(!seq.noteCount[track])
		return;
	
	// find prev note, acounting for ties
	
	ni=seq.noteIndex[track]+1+offset;
	do
	{
		ni=(ni+seq.noteCount[track]-1)%seq.noteCount[track];
		n=seq.notes[track][ni];
	}
	while(ni!=seq.noteIndex[track]+1 && n==SEQ_NOTE_TIE);
	
	// silence it, in case it's not a rest
	
	if(n!=SEQ_NOTE_TIE && n!=SEQ_NOTE_REST)
	{
		assigner_assignNote(n+seq.previousTranspose[track],0,0);
		
		// pass to MIDI out
		midi_sendNoteEvent(n+seq.previousTranspose[track],0,0);
	}
}

inline void seq_setMode(int8_t track, seqMode_t mode)
{
	seqMode_t oldMode=seq.mode[track];
	
	if(mode==oldMode)
		return;

	if(oldMode==smOff)
	{
		// load sequence from storage on start
		if(!storage_loadSequencer(track,seq.notes[track],SEQ_NOTE_MEMORY))
			memset(seq.notes[track],ASSIGNER_NO_NOTE,SEQ_NOTE_MEMORY);

		// compute note count
		seq.noteCount[track]=SEQ_NOTE_MEMORY;
		while(seq.noteCount[track] && seq.notes[track][seq.noteCount[track]-1]==ASSIGNER_NO_NOTE)
			--seq.noteCount[track];
	}
	else if(oldMode==smRecording)
	{
		// store sequence to storage on record end
		storage_saveSequencer(track,seq.notes[track],SEQ_NOTE_MEMORY);
	}
	else if(oldMode==smPlaying)
	{
		finishPreviousNote(track,0);
	}	
	
	if(mode==smPlaying)
		seq_resetCounter(track);

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
	seq.noteIndex[track]=-1; // reinit
	seq.counter=INT16_MAX; // start on a note
}

FORCEINLINE seqMode_t seq_getMode(int8_t track)
{
	return seq.mode[track];
}

FORCEINLINE uint8_t seq_getNoteCount(int8_t track)
{
	return seq.noteCount[track];
}

void seq_inputNote(uint8_t note)
{
	for(int8_t track=0;track<SEQ_TRACK_COUNT;++track)
	{
		if(seq.mode[track]!=smRecording)
			continue;
		
		if(note==SEQ_NOTE_UNDO)
		{
			if(seq.noteCount[track])
				seq.notes[track][--seq.noteCount[track]]=ASSIGNER_NO_NOTE;
		}
		else
		{
			if(seq.noteCount[track]<SEQ_NOTE_MEMORY)
				seq.notes[track][seq.noteCount[track]++]=note;
		}
	}
}

void seq_update(void)
{
	uint8_t n;
	
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

		if(!seq.noteCount[track])
			continue;

		// to next note
		
		seq.noteIndex[track]=(seq.noteIndex[track]+1)%seq.noteCount[track];
		n=seq.notes[track][seq.noteIndex[track]];

		// handle note

		if(n!=SEQ_NOTE_TIE)
		{
			finishPreviousNote(track,-1);
		}
		
		if(n!=SEQ_NOTE_TIE && n!=SEQ_NOTE_REST)
		{
			// send note to assigner, velocity at half (MIDI value 64)
			assigner_assignNote(n+seq.transpose,1,HALF_RANGE);

			// pass to MIDI out
			midi_sendNoteEvent(n+seq.transpose,1,HALF_RANGE);
		
			seq.previousTranspose[track]=seq.transpose;
		}
	}
}

void seq_init(void)
{
	int8_t track;
	
	memset(&seq,0,sizeof(seq));

	for(track=0;track<SEQ_TRACK_COUNT;++track)
	{
		seq.noteIndex[track]=-1;
		memset(seq.notes[track],ASSIGNER_NO_NOTE,SEQ_NOTE_MEMORY);
	}		
}
