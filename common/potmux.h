#ifndef POTMUX_H
#define	POTMUX_H

#include "synth.h"

#define POTMUX_POT_COUNT 32

uint16_t potmux_getValue(p600Pot_t pot);

int8_t potmux_hasChanged(p600Pot_t pot);
p600Pot_t potmux_lastChanged(void);
void potmux_resetChanged(void);
void potmux_resetChangedFull(void);
void potmux_resetSpeedPot(void);

void potmux_init(void);

void potmux_update(uint8_t updateAll);
uint8_t comparePotVal(p600Pot_t pot, uint16_t potValue, uint16_t compareValue);

int8_t potmux_isPotZeroCentered(p600Pot_t pot, uint8_t layout);

#endif	/* POTMUX_H */

