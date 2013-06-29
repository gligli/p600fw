#ifndef ASSIGNER_H
#define	ASSIGNER_H

#include "p600.h"

#define ASSIGNER_NO_NOTE UINT8_MAX

typedef enum
{
	apLast=0,apLow=1,apHigh=2
} assignerPriority_t;

extern const char * assigner_priorityName[3];

void assigner_setPriority(assignerPriority_t prio);
void assigner_setVoiceMask(uint8_t mask);

int8_t assigner_getAssignment(int8_t voice, uint8_t * note);
void assigner_assignNote(uint8_t note, int8_t gate, uint16_t velocity, int8_t forceLegato);
void assigner_voiceDone(int8_t voice); // -1 -> all voices finished

void assigner_setPattern(uint8_t * pattern);
void assigner_getPattern(uint8_t * pattern);
void assigner_setPolyPattern(void);
void assigner_latchPattern(void);

void assigner_init(void);

#endif	/* ASSIGNER_H */

