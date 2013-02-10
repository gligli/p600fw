////////////////////////////////////////////////////////////////////////////////
// Potentiometers multiplexer/scanner
////////////////////////////////////////////////////////////////////////////////

#include "potmux.h"
#include "dac.h"

#define POTMUX_POT_COUNT 32

static struct
{
	uint16_t pots[POTMUX_POT_COUNT];
} potmux;

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
	int i,j,lower;
	uint8_t mux;
	uint16_t estimate;
	uint16_t bit;
	
	for(i=0;i<POTMUX_POT_COUNT;++i)
	{
		// select current pot
		
		mux=(i&0x0f)|(~(0x10<<(i>>4))&0x30);
		io_write(0x0a,mux);
		wait(16);
		
		// successive approximations using DAC and comparator
		
		estimate=0;
		bit=0x8000;

		for(j=0;j<=14;++j) // 14bit DAC
		{
			dac_write(estimate); // update DAC
			wait(4);
			
			lower=(io_read(0x09)&0x08)!=0; // is DAC value lower than pot value?
			
			if (lower)
				estimate+=bit;
			else
				estimate-=bit;
					
			bit>>=1;
		}
		
		potmux.pots[i]=estimate;
	}

	// unselect
	io_write(0x0a,0xff);
}
