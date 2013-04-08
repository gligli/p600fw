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

#define PRIORITY_POT_COUNT 5

static const p600Pot_t priorityPots[PRIORITY_POT_COUNT]=
{
	ppCutoff,ppPitchWheel,ppModWheel,ppFreqA,ppFreqB
};


static struct
{
	int8_t currentRegularPot;
	int8_t currentPriorityPotIdx;
	uint16_t pots[POTMUX_POT_COUNT];
} potmux;

static void updatePot(p600Pot_t pot)
{
	int8_t i,lower;
	uint8_t mux,bitDepth;
	uint16_t estimate,badMask;
	uint16_t bit;

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

	potmux.pots[pot]=estimate&badMask;
}

inline uint16_t potmux_getValue(p600Pot_t pot)
{
	return potmux.pots[pot];
}

void potmux_init(void)
{
	memset(&potmux,0,sizeof(potmux));
}

void potmux_update(int8_t updateRegular, int8_t updatePriority)
{
	if (updateRegular)
	{
		updatePot(potmux.currentRegularPot);

		if(potmux.currentRegularPot==ppPitchWheel)
			potmux.currentRegularPot=ppModWheel; // hole in the enum between those 2
		else
			potmux.currentRegularPot=(potmux.currentRegularPot+1)%POTMUX_POT_COUNT;
	}

	if(updatePriority)
	{
		updatePot(priorityPots[potmux.currentPriorityPotIdx]);

		potmux.currentPriorityPotIdx=(potmux.currentPriorityPotIdx+1)%PRIORITY_POT_COUNT;
	}
}
