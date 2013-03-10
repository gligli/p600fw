////////////////////////////////////////////////////////////////////////////////
// Potentiometers multiplexer/scanner
////////////////////////////////////////////////////////////////////////////////

#include "potmux.h"
#include "dac.h"

#define POTMUX_POT_COUNT 32

static const int8_t potBitDepth[POTMUX_POT_COUNT]=
{
	/*Mixer*/8,/*Cutoff*/12,/*Resonance*/8,/*FilEnvAmt*/10,/*FilRel*/8,/*FilSus*/8,
	/*FilDec*/8,/*FilAtt*/8,/*AmpRel*/8,/*AmpSus*/8,/*AmpDec*/8,/*AmpAtt*/8,
	/*Glide*/8,/*BPW*/10,/*MVol*/8,/*MTune*/12,/*PitchWheel*/12,0,0,0,0,0,/*ModWheel*/8,
	/*Speed*/10,/*APW*/10,/*PModFilEnv*/10,/*LFOFreq*/10,/*PModOscB*/10,/*LFOAmt*/10,/*FreqB*/12,/*FreqA*/12,/*FreqBFine*/8
};

static struct
{
	uint32_t neededBits[POTMUX_POT_COUNT/8];
	uint16_t pots[POTMUX_POT_COUNT];
} potmux;

void inline potmux_setNeeded(uint32_t needed)
{
	potmux.neededBits[3]|=needed>>24;
	potmux.neededBits[2]|=needed>>16;
	potmux.neededBits[1]|=needed>>8;
	potmux.neededBits[0]|=needed;
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
	uint8_t mux,needed,bitDepth;
	uint16_t estimate,badMask;
	uint16_t bit;
	
	for(j=0;j<POTMUX_POT_COUNT;++j)
	{
		// there's a hole in the pots enum

		if (j>ppPitchWheel && j<ppModWheel)
			continue;

		// don't update unneeded pots

		needed=potmux.neededBits[j>>3];
		if(!needed || ((needed&(1<<(j&7)))==0))
			continue;

		// successive approximations using DAC and comparator

		BLOCK_INT
		{
				// select pot

			mux=(j&0x0f)|(0x20>>(j>>4));
			io_write(0x0a,mux);
			CYCLE_WAIT(4);

				// init values

			estimate=UINT16_MAX;
			bit=0x8000;
			bitDepth=potBitDepth[j];
			badMask=16-bitDepth;
			badMask=(UINT16_MAX>>badMask)<<badMask;


				// main loop

			for(i=0;i<=bitDepth;++i)
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

			potmux.pots[j]=estimate&badMask;
		}
	}
	
	potmux.neededBits[0]=0;
	potmux.neededBits[1]=0;
	potmux.neededBits[2]=0;
	potmux.neededBits[3]=0;
}
