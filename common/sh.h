#ifndef SH_H
#define	SH_H

#include "synth.h"

#define SH_FLAG_IMMEDIATE 1

void sh_setCV(p600CV_t cv,uint16_t value, uint8_t flags);
void sh_setCV32Sat(p600CV_t cv,int32_t value, uint8_t flags);

// those two should only be used while in interrupt!
void sh_setCV_FastPath(p600CV_t cv,uint16_t value);
void sh_setCV32Sat_FastPath(p600CV_t cv,int32_t value);

void sh_setGate(p600Gate_t gate,int8_t on);

void sh_init(void);
void sh_update(void);

#endif	/* SH_H */

