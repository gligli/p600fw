////////////////////////////////////////////////////////////////////////////////
// Potentiometers multiplexer/scanner
////////////////////////////////////////////////////////////////////////////////

#include "potmux.h"
#include "dac.h"

#define PRIORITY_POT_COUNT 4
#define CHANGE_DETECT_THRESHOLD 4

static const int8_t potBitDepth[POTMUX_POT_COUNT]=
{
	/*Mixer*/8,/*Cutoff*/12,/*Resonance*/8,/*FilEnvAmt*/10,/*FilRel*/8,/*FilSus*/8,
	/*FilDec*/8,/*FilAtt*/8,/*AmpRel*/8,/*AmpSus*/8,/*AmpDec*/8,/*AmpAtt*/8,
	/*Glide*/8,/*BPW*/10,/*MVol*/8,/*MTune*/12,/*PitchWheel*/12,0,0,0,0,0,/*ModWheel*/8,
	/*Speed*/12,/*APW*/10,/*PModFilEnv*/10,/*LFOFreq*/10,/*PModOscB*/10,/*LFOAmt*/10,/*FreqB*/12,/*FreqA*/12,/*FreqBFine*/8
};

static const p600Pot_t priorityPots[PRIORITY_POT_COUNT]=
{
	ppCutoff,ppPitchWheel,ppFreqA,ppFreqB
};


static struct
{
	uint32_t potChanged;
	uint8_t changeDetect[POTMUX_POT_COUNT];

	uint16_t pots[POTMUX_POT_COUNT];
	int8_t currentRegularPot;
	p600Pot_t lastChanged;
} potmux;

static void updatePot(p600Pot_t pot)
{
	int8_t i,lower;
	uint8_t mux,bitDepth,cdv;
	uint16_t estimate,badMask;
	uint16_t bit;
	
	BLOCK_INT
	{
		// successive approximations using DAC and comparator

			// select pot

		mux=(pot&0x0f)|(0x20>>(pot>>4));
		io_write(0x0a,mux);
		CYCLE_WAIT(4);

			// init values

		estimate=UINT16_MAX;
		bit=0x8000;
		bitDepth=potBitDepth[pot];
		badMask=16-bitDepth;
		badMask=(UINT16_MAX>>badMask)<<badMask;


			// main loop

		for(i=0;i<=bitDepth;++i)
		{
			dac_write(estimate);				

			// let comparator get correct voltage (don't remove me!)
			CYCLE_WAIT(2);

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
		CYCLE_WAIT(4);

		estimate&=badMask;
		potmux.pots[pot]=estimate;
		
		// change detector
		
		cdv=estimate>>8;
		
		if(abs(potmux.changeDetect[pot]-cdv)>CHANGE_DETECT_THRESHOLD)
		{
			potmux.changeDetect[pot]=cdv;
			potmux.potChanged|=(uint32_t)1<<pot;
			potmux.lastChanged=pot;
		}
	}
}

FORCEINLINE uint16_t potmux_getValue(p600Pot_t pot)
{
	return potmux.pots[pot];
}

FORCEINLINE int8_t potmux_hasChanged(p600Pot_t pot)
{
	return (potmux.potChanged&((uint32_t)1<<pot))!=0;
}

FORCEINLINE p600Pot_t potmux_lastChanged(void)
{
	return potmux.lastChanged;
}

FORCEINLINE void potmux_resetChanged(void)
{
	potmux.potChanged=0;
	potmux.lastChanged=ppNone;
}

int8_t potmux_isPotZeroCentered(p600Pot_t pot)
{
	return pot==ppFilEnvAmt || pot==ppPModFilEnv || pot==ppFreqBFine || pot==ppMTune || pot==ppPitchWheel;
}

inline void potmux_update(uint8_t regularPotCount)
{
	int16_t i;
	
	for(i=0;i<regularPotCount;++i)
	{
		updatePot(potmux.currentRegularPot);

		if(potmux.currentRegularPot==ppPitchWheel)
			potmux.currentRegularPot=ppModWheel; // hole in the enum between those 2
		else
			potmux.currentRegularPot=(potmux.currentRegularPot+1)%POTMUX_POT_COUNT;
	}

	for(i=0;i<PRIORITY_POT_COUNT;++i)
		updatePot(priorityPots[i]);
}

void potmux_init(void)
{
	memset(&potmux,0,sizeof(potmux));
	potmux.lastChanged=ppNone;
}
