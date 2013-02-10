#ifndef SYNTH_H
#define	SYNTH_H

#include "p600.h"

void synth_setCV(p600CV_t cv,uint16_t value);
void synth_setGate(p600Gate_t gate,int on);

void synth_init(void);
void synth_update(void);

#endif	/* SYNTH_H */

