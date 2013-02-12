#ifndef SYNTH_H
#define	SYNTH_H

#include "p600.h"

void synth_setCV(p600CV_t cv,uint16_t value, int8_t immediate);
void synth_setGate(p600Gate_t gate,int8_t on, int8_t immediate);

void synth_init(void);
void synth_update(void);

#endif	/* SYNTH_H */

