////////////////////////////////////////////////////////////////////////////////
// Potentiometers multiplexer/scanner
////////////////////////////////////////////////////////////////////////////////

#include "potmux.h"
#include "dac.h"

#define POTMUX_POT_COUNT 32

static struct
{
	uint32_t neededBits;
	uint16_t pots[POTMUX_POT_COUNT];
} potmux;

void potmux_setNeeded(uint32_t needed)
{
	potmux.neededBits|=needed;
}

uint16_t potmux_getValue(p600Pot_t pot)
{
	return potmux.pots[pot];
}

void potmux_init(void)
{
	memset(&potmux,0,sizeof(potmux));
}

void potmux_update(void)
{
	uint8_t i,j;
	uint8_t lower,mux;
	uint16_t estimate;
	uint16_t bit;
	
	for(j=0;j<POTMUX_POT_COUNT;++j)
	{
		// don't update unneeded pots
		
		if(((potmux.neededBits>>j)&1)==0)
			continue;
		
		// there's a hole in the pots enum
		
		if (j>ppPitchWheel && j<ppModWheel)
			continue;
		
		HW_ACCESS
		{

			// select pot

			mux=(j&0x0f)|(~(0x10<<(j>>4))&0x30);
			io_write(0x0a,mux);
			CYCLE_WAIT(8);

			// successive approximations using DAC and comparator

			estimate=0;
			bit=0x8000;

			for(i=0;i<12;++i) // 12bit -> 4096 steps
			{
				// write DAC
				mem_write(0x4000,estimate>>2);
				mem_write(0x4001,estimate>>10);

				lower=(io_read(0x09)&0x08)!=0; // is DAC value lower than pot value?

				if (lower)
					estimate+=bit;
				else
					estimate-=bit;

				bit>>=1;
			}

			potmux.pots[j]=estimate;

			// unselect

			io_write(0x0a,0xff);
		}
	}
	
	potmux.neededBits=0;
}
