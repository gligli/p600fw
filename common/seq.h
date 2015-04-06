#ifndef SEQ_H
#define	SEQ_H

#include "synth.h"

// Sequencer event list definitions

#define SEQ_NOTEBITS 0x3f /* bits 0..5 */

// event is a continuation of previous event - only used for notes
#define SEQ_CONT 128 /* bit 7 */

/* These must be > highest note which is 60 */
// A lone rest means an unused entry (i.e. at the end of the sequence).
// In a valid sequence a rest always is the first (and only) entry in a step,
// consequently it always comes with SEQ_CONT clear.
#define SEQ_REST (ASSIGNER_NO_NOTE&0x3f)
// A tie extends the timing of the previous step
#define SEQ_TIE (SEQ_REST-1)

// sequencer config
#define SEQ_NOTE_MEMORY 128
#define SEQ_TRACK_COUNT 2

// Codes from keypad presses
#define SEQ_NOTE_STEP UINT8_MAX-1
#define SEQ_NOTE_UNDO UINT8_MAX-2
#define SEQ_NOTE_CLEAR UINT8_MAX-3

typedef enum
{
	smOff=0,smWaiting=1,smPlaying=2,smRecording=3
} seqMode_t;


void seq_init(void);
void seq_update(void);

void seq_setMode(int8_t track, seqMode_t mode);
void seq_setSpeed(uint16_t speed);
void seq_setTranspose(int8_t transpose);
seqMode_t seq_getMode(int8_t track);
uint8_t seq_getStepCount(int8_t track);
int8_t seq_full(int8_t track);
void seq_resetCounter(int8_t track, int8_t beatReset);
void seq_silence(int8_t track);

void seq_inputNote(uint8_t note, uint8_t pressed);

#endif	/* SEQ_H */

