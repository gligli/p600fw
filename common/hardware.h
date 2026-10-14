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

// V2.5 (Teensy++ 2.0): presets, settings and sequences live in the internal flash of the AT90USB1286, in the STORAGE_SIZE bytes just
// below the updater (0x1e000, see STORAGE_ADDR in firmware/p600firmware.c), so the storage area is 0x16000..0x1dfff and the program
// must end below 0x16000 (90112 bytes; the Makefile checks this after every build).
// V2.34 used 224 pages (0x10000..0x1dfff), but this firmware is about 78 KB and would overwrite the first presets, so the area is now
// 128 pages. The top of the area is where it was in V2.34, so the sequencer pages, the manual preset and the settings (tuning) are at
// the same flash addresses as in V2.34 (see SEQUENCER_START_PAGE in storage.h).
// (The Flat-Labs board used an external 64 KB F-RAM here: STORAGE_SIZE 0x10000, 256 pages.)
#define STORAGE_PAGE_SIZE 0x100UL 	//  256 bytes
#define STORAGE_SIZE 0x8000UL 		//	32KB, 128 pages: presets 0-99, sequencer 104-105, manual preset 123, settings 124-125

extern void storage_write(uint32_t pageIdx, uint8_t *buf);
extern void storage_read(uint32_t pageIdx, uint8_t *buf);

#endif	/* HARDWARE_H */

