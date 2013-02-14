#ifndef HARDWARE_IMPL_H
#define	HARDWARE_IMPL_H

#include <util/atomic.h>

#define DO_ONE_WAIT(x,c) \
	if ((x)<=(c)) asm volatile("nop\n nop\n nop\n nop\n"); // the test will be eliminated by compiler

#define CYCLE_WAIT(cycles) \
{ \
	DO_ONE_WAIT(0x01,cycles); \
	DO_ONE_WAIT(0x02,cycles); \
	DO_ONE_WAIT(0x03,cycles); \
	DO_ONE_WAIT(0x04,cycles); \
	DO_ONE_WAIT(0x05,cycles); \
	DO_ONE_WAIT(0x06,cycles); \
	DO_ONE_WAIT(0x07,cycles); \
	DO_ONE_WAIT(0x08,cycles); \
	DO_ONE_WAIT(0x09,cycles); \
	DO_ONE_WAIT(0x0a,cycles); \
	DO_ONE_WAIT(0x0b,cycles); \
	DO_ONE_WAIT(0x0c,cycles); \
	DO_ONE_WAIT(0x0d,cycles); \
	DO_ONE_WAIT(0x0e,cycles); \
	DO_ONE_WAIT(0x0f,cycles); \
	DO_ONE_WAIT(0x10,cycles); \
	DO_ONE_WAIT(0x11,cycles); \
	DO_ONE_WAIT(0x12,cycles); \
	DO_ONE_WAIT(0x13,cycles); \
	DO_ONE_WAIT(0x14,cycles); \
	DO_ONE_WAIT(0x15,cycles); \
	DO_ONE_WAIT(0x16,cycles); \
	DO_ONE_WAIT(0x17,cycles); \
	DO_ONE_WAIT(0x18,cycles); \
	DO_ONE_WAIT(0x19,cycles); \
	DO_ONE_WAIT(0x1a,cycles); \
	DO_ONE_WAIT(0x1b,cycles); \
	DO_ONE_WAIT(0x1c,cycles); \
	DO_ONE_WAIT(0x1d,cycles); \
	DO_ONE_WAIT(0x1e,cycles); \
	DO_ONE_WAIT(0x1f,cycles); \
	DO_ONE_WAIT(0x20,cycles); \
}

#define HW_ACCESS ATOMIC_BLOCK(ATOMIC_RESTORESTATE)

#endif	/* HARDWARE_IMPL_H */

