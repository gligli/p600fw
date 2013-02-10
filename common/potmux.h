#ifndef POTMUX_H
#define	POTMUX_H

#include "p600.h"

uint16_t potmux_getValue(p600Pot_t pot);

void potmux_init(void);
void potmux_update(void);

#endif	/* POTMUX_H */

