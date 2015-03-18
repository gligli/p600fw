#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

void clock_setSpeed(uint16_t speed);
uint16_t clock_getSpeed(void);
uint16_t clock_getCounter(void);
void clock_reset(void);
int8_t clock_update(void);

#endif /* CLOCK_H */
