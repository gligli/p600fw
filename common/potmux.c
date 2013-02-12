////////////////////////////////////////////////////////////////////////////////
// Potentiometers multiplexer/scanner
////////////////////////////////////////////////////////////////////////////////

#include "potmux.h"
#include "dac.h"

#define POTMUX_POT_COUNT 32
#define POTMUX_POTS_AT_A_TIME 10

static struct
{
	uint16_t pots[POTMUX_POT_COUNT];
	uint8_t nextPot;
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
	uint8_t i,j;
	uint8_t pot,lower,mux;
	uint16_t estimate;
	uint16_t bit;
	int32_t move;
	
	for(j=0;j<POTMUX_POTS_AT_A_TIME;++j)
	{
		pot=potmux.nextPot;

		int_clear();

		// select pot

		mux=(pot&0x0f)|(~(0x10<<(pot>>4))&0x30);
		io_write(0x0a,mux);
		wait(16);

		// successive approximations using DAC and comparator

		estimate=0;
		bit=0x8000;

		for(i=0;i<14;++i) // 14bit DAC
		{
			dac_write(estimate); // update DAC
			lower=(io_read(0x09)&0x08)!=0; // is DAC value lower than pot value?

			if (lower)
				estimate+=bit;
			else
				estimate-=bit;

			bit>>=1;
		}

		move=(int32_t)potmux.pots[pot]-(int32_t)estimate;
		potmux.pots[pot]=estimate;

		// unselect

		io_write(0x0a,0xff);

		int_set();
		
		// next pot

		++pot;
		if (pot>=POTMUX_POT_COUNT)
			pot=0;
		else if (pot>ppPitchWheel && pot<ppModWheel)
			pot=ppModWheel;

		potmux.nextPot=pot;
	}
}
