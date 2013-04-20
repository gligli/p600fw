#ifndef SYNTH_H
#define	SYNTH_H

#include "p600.h"

#define SYNTH_FLAG_IMMEDIATE 1

void synth_setCV(p600CV_t cv,uint16_t value, uint8_t flags);
void synth_setCV32Sat(p600CV_t cv,int32_t value, uint8_t flags);

// those two should only be used while in interrupt!
void synth_setCV_FastPath(p600CV_t cv,uint16_t value);
void synth_setCV32Sat_FastPath(p600CV_t cv,int32_t value);

void synth_setGate(p600Gate_t gate,int8_t on);
void synth_updateCV(p600CV_t cv);

void synth_init(void);
void synth_update(void);

#endif	/* SYNTH_H */

