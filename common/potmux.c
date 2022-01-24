////////////////////////////////////////////////////////////////////////////////
// Potentiometers multiplexer/scanner
////////////////////////////////////////////////////////////////////////////////

#include "potmux.h"
#include "dac.h"
#include "midi.h"

#define CHANGE_DETECT_THRESHOLD 4

static const int8_t potBitDepth[POTMUX_POT_COUNT]=
{
	/*Mixer*/8, /*Cutoff*/12,/*Resonance*/8,/*FilEnvAmt*/10,/*FilRel*/8,/*FilSus*/10,
	/*FilDec*/8,/*FilAtt*/8,/*AmpRel*/8,/*AmpSus*/10,/*AmpDec*/8,/*AmpAtt*/8,
	/*Glide*/8,/*BPW*/10,/*MVol*/8,/*MTune*/12,/*PitchWheel*/12,0,0,0,0,0,/*ModWheel*/8,
	/*Speed*/12,/*APW*/10,/*PModFilEnv*/10,/*LFOFreq*/10,/*PModOscB*/10,/*LFOAmt*/12,/*FreqB*/14,/*FreqA*/14,/*FreqBFine*/10
};

static const p600Pot_t priorityPots[1]=
{
//	ppFreqA, ppFreqB, ppModWheel, ppPitchWheel, ppNone
//	ppFreqB, ppPitchWheel, ppNone
    ppNone
};

static const p600Pot_t regularPots[28]=
{
    ppMixer, ppResonance, ppFilEnvAmt, ppGlide, ppBPW, ppAPW, ppMVol, ppPModFilEnv, ppLFOFreq, ppPModOscB, ppLFOAmt, ppSpeed,  ppAmpRel, ppAmpSus, ppAmpDec, ppAmpAtt, ppFilAtt, ppFilDec, ppFilSus, ppFilRel, ppMTune, ppFreqBFine, ppCutoff, ppFreqA, ppModWheel, ppFreqB, ppPitchWheel, ppNone
};

static struct
{
	uint32_t potChanged;
	uint8_t changeDetect[POTMUX_POT_COUNT];

	uint16_t pots[POTMUX_POT_COUNT];
	uint8_t potExcited[POTMUX_POT_COUNT];
	uint8_t potExcitedCount[POTMUX_POT_COUNT];
	int8_t currentRegularPot;
	p600Pot_t lastChanged;
} potmux;

static void updatePot(p600Pot_t pot)
{
	int8_t i,lower;
	uint8_t mux,bitDepth,cdv,diff;
	uint16_t estimate,badMask;
	uint16_t bit;
    p600Pot_t lastActivePot;
	
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
		
        // suppress the bits beyond the measurement accuracy
		estimate&=badMask;

        // change detector
		cdv=estimate>>8;
        diff = abs(potmux.changeDetect[pot]-cdv);
        lastActivePot=potmux.lastChanged;
		if(diff>CHANGE_DETECT_THRESHOLD || (potmux.potExcitedCount[pot]>0 && pot!=ppPitchWheel))
		{
			potmux.changeDetect[pot]=cdv;
			potmux.potChanged|=(uint32_t)1<<pot;
            if(diff>CHANGE_DETECT_THRESHOLD) potmux.potExcitedCount[pot]=100; // keep up excited state for at least 50 cycles (about a quarter of a second)
			potmux.lastChanged=pot;
		}
		potmux.potExcited[pot]=potmux.potExcitedCount[pot]>0?1:0;
		if (lastActivePot!=pot)
            if (potmux.potExcitedCount[pot]>0) potmux.potExcitedCount[pot]--;

        // the idea is to add the leading (16-bitDepth) bits to the lower end to make the range 0 ... UNIT16_MAX despite the limited accuracy
        if (estimate>=0xFC00) estimate=badMask; // choose max value above the threshold UINT16_t - CHANGE_DETECT_THRESHOLD
		potmux.pots[pot]=estimate|(estimate>>bitDepth);
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

int8_t potmux_isPotZeroCentered(p600Pot_t pot, uint8_t layout)
{
	return pot==ppFilEnvAmt || pot==ppPModFilEnv || pot==ppFreqBFine || pot==ppMTune || pot==ppPitchWheel || (pot==ppMixer && layout==1);
}

inline void potmux_update(uint8_t updateAll)
{
	int16_t i, updatable;
    updatable=updateAll?27:1;
	for(i=0;i<updatable;++i)
	{
		if (!potmux.potExcited[regularPots[potmux.currentRegularPot]]) updatePot(regularPots[potmux.currentRegularPot]); // done in the next loop
		potmux.currentRegularPot++;
        if (regularPots[potmux.currentRegularPot]==ppNone) potmux.currentRegularPot=0;
	}

    p600Pot_t pp=regularPots[0];
    i=0;
    while (pp!=ppNone) // this cycle once through the pots in prio 2
    {
        if (potmux.potExcited[pp]) updatePot(pp);
        i++;
        pp=regularPots[i];
    }

    i=0;
    pp=priorityPots[0];
    while (pp!=ppNone) // this cycle once through the pots in prio 1
    {
        updatePot(pp);
        i++;
        pp=priorityPots[i];
    }
}

void potmux_init(void)
{
	memset(&potmux,0,sizeof(potmux));
	potmux.lastChanged=ppNone;
}
