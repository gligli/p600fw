#ifndef ASSIGNER_H
#define	ASSIGNER_H

#include "p600.h"

#define ASSIGNER_NO_NOTE UINT8_MAX

typedef enum
{
	mUnisonLow=0,mUnisonHigh=1,mMonoLow=2,mMonoHigh=3,mPoly=15,
} assignerMode_t;


int8_t assigner_getAssignment(int8_t voice, uint8_t * note);

void assigner_setMode(assignerMode_t mode);
assignerMode_t assigner_getMode(void);
const char * assigner_modeName(assignerMode_t mode);

void assigner_assignNote(uint8_t note, int8_t on, uint16_t velocity);
void assigner_voiceDone(int8_t voice); // -1 -> all voices finished

void assigner_init(void);

#endif	/* ASSIGNER_H */

