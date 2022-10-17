////////////////////////////////////////////////////////////////////////////////
// Potentiometers multiplexer/scanner
////////////////////////////////////////////////////////////////////////////////

#include "potmux.h"
#include "dac.h"
#include "midi.h"

#define CHANGE_DETECT_THRESHOLD 4

static const int8_t potBitDepth[POTMUX_POT_COUNT]=
{
    /*Vol A / Mixer*/8,
    /*Cutoff*/12,
    /*Resonance*/8,
    /*FilEnvAmt*/10,
    /*FilRel*/8,
    /*FilSus*/10,
    /*FilDec*/8,
    /*FilAtt*/8,
    /*AmpRel*/8,
    /*AmpSus*/10,
    /*AmpDec*/8,
    /*AmpAtt*/8,
    /*Vol B / Glide*/8,
    /*BPW*/10,
    /*MVol*/8,
    /*MTune*/12,
    /*PitchWheel*/12,
    0,
    0,
    0,
    0,
    0,
    /*ModWheel*/8,
    /*Speed*/12,
    /*APW*/10,
    /*PModFilEnv*/10,
    /*LFOFreq*/10,
    /*PModOscB*/10,
    /*LFOAmt*/12,
    /*FreqB*/12,
    /*FreqA*/12,
    /*FreqBFine*/12,

};

static const int8_t response[POTMUX_POT_COUNT]=
{
    /*Vol A*/6,
    /*Cutoff*/4,
    /*Resonance*/3,
    /*Fil Env Amt*/6,
    /*Filter Release */35,
    /*Filter Sustain*/6,
    /*Filter Decay*/35,
    /*Filter Attack*/35,
    /*Amp Release*/35,
    /*Amp Sustain*/6,
    /*Amp Decay*/35,
    /*Amp Attack*/35,
    /*Vol B*/6,
    /*BPW*/7,
    /*Master Vol*/6,
    /*Master Tune*/11,
    /*Pitch Bender*/3,
    0,
    0,
    0,
    0,
    0,
    /*Mod Wheel*/3,
    /*Data Dial*/8,
    /*APW*/7,
    /*Poly-Mod Env*/8,
    /*Poly-Mod OSC B*/3,
    /*LFO Frequency*/20,
    /*LFO Amount*/6,
    /*Frequency B*/3,
    /*Frequency A*/3,
    /*Frequency B Fine*/20,
};


static struct
{
	uint32_t potChanged;
	uint8_t changeDetect[POTMUX_POT_COUNT];

	uint16_t pots[POTMUX_POT_COUNT];
	uint8_t potExcited[POTMUX_POT_COUNT];
	uint8_t potcounter[POTMUX_POT_COUNT];
	int8_t currentRegularPot;
	int8_t lastChanged;
	int8_t lastInAction;

} potmux;

static void updatePot(int8_t pot)
{
	int8_t i,lower;
	uint8_t mux,bitDepth,cdv,diff;
	uint16_t estimate,badMask;
	uint16_t bit;

    if (pot<0) return;

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
		if (potmux.lastInAction!=pot) potmux.potExcited[pot]=0;
		if(diff>CHANGE_DETECT_THRESHOLD || (potmux.potExcited[pot] && pot!=ppPitchWheel))
		{
			potmux.changeDetect[pot]=cdv;
			potmux.potChanged|=(uint32_t)1<<pot;
            potmux.potExcited[pot]=1;
            potmux.lastChanged=pot; // this is reset in every cycle, this is used to decide which value to show on the display
            potmux.lastInAction=pot; // this stays and is only reset for a change of mode
		}

        if (estimate>=0xFC00)
        {
            potmux.pots[pot]=badMask; // choose max value above the threshold UINT16_t - CHANGE_DETECT_THRESHOLD
        }
        else
        {
            potmux.pots[pot]=estimate;
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

void potmux_resetChangedFull(void)
{
    potmux_resetChanged();
    potmux.lastInAction=ppNone;
    uint8_t i=0;
    for (i=0;i<POTMUX_POT_COUNT;i++)
    {
        potmux.potExcited[i]=0;
    }
}


FORCEINLINE void potmux_resetSpeedPot(void)
{
    uint32_t mask=1;
	potmux.potChanged&=(~(mask<<ppSpeed)); // remove the bit of the speed pot
	if (potmux.lastChanged==ppSpeed) potmux.lastChanged=ppNone;
    potmux.potExcited[ppSpeed]=0;

}

int8_t potmux_isPotZeroCentered(p600Pot_t pot, uint8_t layout)
{
	return pot==ppFilEnvAmt || pot==ppPModFilEnv || pot==ppFreqBFine || pot==ppMTune || pot==ppPitchWheel || (pot==ppMixer && layout==1);
}

inline void potmux_update(uint8_t updateAll)
{
	int16_t i;

    for(i=0;i<POTMUX_POT_COUNT;++i)
    {
        if (response[i]!=0)
        {
            if (potmux.potcounter[i]==0 || potmux.potExcited[i] || updateAll)
            {
                updatePot(i);
            }
        }
        potmux.potcounter[i]=(potmux.potcounter[i]+1)%(2*response[i]);
    }
}

void potmux_init(void)
{
	memset(&potmux,0,sizeof(potmux));
    potmux_resetChangedFull();
    uint8_t i;

    for (i=0;i<POTMUX_POT_COUNT;++i)
    {
        potmux.potcounter[i]=i; // distribute the pots as evenly as possible
    }
}
