#ifndef ASSIGNER_H
#define	ASSIGNER_H

#include "synth.h"
#include "storage.h"


#define ASSIGNER_NO_NOTE UINT8_MAX

typedef enum
{
	apLast=0,apLow=1,apHigh=2
} assignerPriority_t;

void assigner_setPriority(assignerPriority_t prio);
void assigner_setVoiceMask(uint8_t mask);

int8_t assigner_getAssignment(int8_t voice, uint8_t * note);
int8_t assigner_getAnyPressed(void);
int8_t assigner_getAnyAssigned(void);
int8_t assigner_getLatestNotePressed(uint8_t * note);

void assigner_assignNote(uint8_t note, int8_t gate, uint16_t velocity, int8_t keyboard);
void assigner_voiceDone(int8_t voice); // -1 -> all voices finished
void assigner_allKeysOff(void);

void assigner_setPattern(uint8_t * pattern, int8_t mono);
void assigner_getPattern(uint8_t * pattern, int8_t * mono);
void assigner_setPoly(void);
void assigner_latchPattern(uint8_t retrigger);
void assigner_holdEvent(int8_t hold);
void assigner_allVoicesDone(void);

void assigner_init(void);

#endif	/* ASSIGNER_H */

