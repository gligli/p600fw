#ifndef HARDWARE_IMPL_H
#define	HARDWARE_IMPL_H

//AVR compatibility
#define PROGMEM
#define pgm_read_byte(addr) (*(uint8_t*)(addr))

#define CYCLE_WAIT(cycles)
#define BLOCK_INT
#define MDELAY(ms)

#endif	/* HARDWARE_IMPL_H */

