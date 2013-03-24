////////////////////////////////////////////////////////////////////////////////
// Synthesizer CVs/Gates
////////////////////////////////////////////////////////////////////////////////

#include "synth.h"
#include "dac.h"

#define SYNTH_CV_COUNT 32

static struct
{
	uint32_t immediateBits;
	uint16_t cvs[SYNTH_CV_COUNT];
	uint8_t gateBits;
	uint8_t lastWrittenGateBits;
} synth;

static inline void updateGates(void)
{
	if(synth.gateBits!=synth.lastWrittenGateBits)
	{
		io_write(0x0b,synth.gateBits);
		synth.lastWrittenGateBits=synth.gateBits;
	}
}

static inline void updateCV(p600CV_t cv, uint16_t cvv, int8_t wait)
{
	uint8_t dmux;
	
	dmux=(cv&0x07)|(~(0x08<<(cv>>3))&0xf8);

	BLOCK_INT
	{
		dac_write(cvv);

		// select current CV
		io_write(0x0d,dmux);
		
		if(wait)
		{
			// 2.5 us to let S&H get very precise voltage, some P600s need it apparently
			CYCLE_WAIT(10);
		}

		// deselect it
		io_write(0x0d,0xff);

		if(wait)
		{
			// 2.5 more us to let analog hardware stabilize
			CYCLE_WAIT(10);
		}
	}
}

inline void synth_setCV(p600CV_t cv,uint16_t value, int8_t immediate, int8_t wait)
{
	if(immediate)
		updateCV(cv,value,wait);
	else
		synth.cvs[cv]=value;
}

inline void synth_setCV32Sat(p600CV_t cv,int32_t value, int8_t immediate, int8_t wait)
{
	value=MAX(value,0);
	value=MIN(value,UINT16_MAX);
	
	synth_setCV(cv,value,immediate,wait);
}

inline void synth_setGate(p600Gate_t gate,int8_t on)
{
	uint8_t mask=1<<gate;
	
	synth.gateBits&=~mask;
	if (on) synth.gateBits|=mask;
	
	updateGates();
}

void synth_updateCV(p600CV_t cv)
{
	updateCV(cv,synth.cvs[cv],1);
}

void synth_init()
{
	memset(&synth,0,sizeof(synth));
	synth.lastWrittenGateBits=UINT8_MAX;
}

void synth_update()
{
	uint8_t i;

	for(i=0;i<SYNTH_CV_COUNT;++i)
		updateCV(i,synth.cvs[i],1);

	updateGates();
}

