#ifndef HARDWARE_H
#define	HARDWARE_H

#include <stdint.h>

////////////////////////////////////////////////////////////////////////////////
// Low level interface, implemented by firmware or mockup
////////////////////////////////////////////////////////////////////////////////

void mem_write(uint16_t address, uint8_t value);
void io_write(uint8_t address, uint8_t value);
uint8_t mem_read(uint16_t address);
uint8_t io_read(uint8_t address);

void wait(uint16_t cycles); // active wait of a number of 4Mhz cycles

#endif	/* HARDWARE_H */

