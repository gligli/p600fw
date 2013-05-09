#ifndef HARDWARE_H
#define	HARDWARE_H

#include <stdint.h>

////////////////////////////////////////////////////////////////////////////////
// Low level interface, implemented by firmware or mockup
////////////////////////////////////////////////////////////////////////////////

#include "hardware_impl.h" // should implement CYCLE_WAIT(cycles) , BLOCK_INT{} , MDELAY(ms)

extern void mem_write(uint16_t address, uint8_t value);
extern void io_write(uint8_t address, uint8_t value);
extern uint8_t mem_read(uint16_t address);
extern uint8_t io_read(uint8_t address);

extern int8_t hardware_getNMIState(void);

#define STORAGE_PAGE_SIZE 256UL
#define STORAGE_SIZE 0xe000UL //56KB, 224 pages

extern void storage_write(uint32_t pageIdx, uint8_t *buf);
extern void storage_read(uint32_t pageIdx, uint8_t *buf);

#endif	/* HARDWARE_H */

