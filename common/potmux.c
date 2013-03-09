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

void inline potmux_setNeeded(uint32_t needed)
{
	potmux.neededBits|=needed;
}

uint16_t inline potmux_getValue(p600Pot_t pot)
{
	return potmux.pots[pot];
}

void potmux_init(void)
{
	memset(&potmux,0,sizeof(potmux));
}

void potmux_update(void)
{
	int8_t i,j,lower;
	uint8_t mux;
	uint16_t estimate;
	uint16_t bit;
	
	for(j=0;j<POTMUX_POT_COUNT;++j)
	{
		// there's a hole in the pots enum
		
		if (j>ppPitchWheel && j<ppModWheel)
			continue;
		
		// don't update unneeded pots
		
		if(((potmux.neededBits>>j)&1)==0)
			continue;
		
		BLOCK_INT
		{
			// successive approximations using DAC and comparator

				// select pot

			mux=(j&0x0f)|(0x20>>(j>>4));
			io_write(0x0a,mux);
			CYCLE_WAIT(8);
			
				// init values
			
			estimate=UINT16_MAX;
			bit=0x8000;
			
				// main loop
			
			for(i=0;i<=12;++i) // more than 12bit is doable, but it's almost useless because of pot noise...
			{
				dac_write(estimate);				

				// let comparator get correct voltage (don't remove me!)
				CYCLE_WAIT(1);

				// is DAC value lower than pot value?
  				lower=(io_read(0x09)&0x08)!=0;
				
				// adjust estimate
				if (lower)
					estimate+=bit;
				else
					estimate-=bit;
				
				// on to finer changes
				bit>>=1;
			}

			// unselect

			io_write(0x0a,0xff);

			potmux.pots[j]=estimate;
		}
	}
	
	potmux.neededBits=0;
}
