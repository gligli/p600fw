#ifndef HARDWARE_IMPL_H
#define	HARDWARE_IMPL_H

#include <util/atomic.h>
#include <util/delay.h>

#define CYCLE_WAIT(cycles) __builtin_avr_delay_cycles(4*cycles);
#define BLOCK_INT ATOMIC_BLOCK(ATOMIC_RESTORESTATE)
#define MDELAY(ms) _delay_ms(ms)

#endif	/* HARDWARE_IMPL_H */

