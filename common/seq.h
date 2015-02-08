#ifndef SEQ_H
#define	SEQ_H

#include "synth.h"

#define SEQ_NOTE_MEMORY 128
#define SEQ_NOTE_CLEAR 129
#define SEQ_TRACK_COUNT 2

#define SEQ_NOTE_REST UINT8_MAX-1
#define SEQ_NOTE_TIE UINT8_MAX-2
#define SEQ_NOTE_UNDO UINT8_MAX-3

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
uint8_t seq_getNoteCount(int8_t track);
void seq_resetCounter(int8_t track);

void seq_inputNote(uint8_t note);

#endif	/* SEQ_H */

