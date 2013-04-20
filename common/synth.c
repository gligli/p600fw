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
} synth;

static inline void updateGates(void)
{
	BLOCK_INT
	{
		io_write(0x0b,synth.gateBits);
	}
}

static inline void updateCV(p600CV_t cv, uint16_t cvv)
{
	uint8_t dmux;
	
	dmux=(cv&0x07)|(~(0x08<<(cv>>3))&0xf8);

	BLOCK_INT
	{
		dac_write(cvv);

		// select current CV
		io_write(0x0d,dmux);

		// 2.5 us to let S&H get very precise voltage, some P600s need it apparently
		CYCLE_WAIT(10);

		// deselect it
		io_write(0x0d,0xff);

		// 2.5 more us to let analog hardware stabilize
		CYCLE_WAIT(10);
	}
}

inline void synth_setCV(p600CV_t cv,uint16_t value, uint8_t flags)
{
	if(flags&SYNTH_FLAG_IMMEDIATE)
	{
		updateCV(cv,value);
	}
	else
	{
		synth.cvs[cv]=value;
	}
}

inline void synth_setCV32Sat(p600CV_t cv,int32_t value, uint8_t flags)
{
	if(value<0)
		value=0;
	else if (value>UINT16_MAX)
		value=UINT16_MAX;

	synth_setCV(cv,value,flags);
}

inline void synth_setCV_FastPath(p600CV_t cv,uint16_t value)
{
	uint8_t dmux;
	
	dmux=(cv&0x07)|(~(0x08<<(cv>>3))&0xf8);

	dac_write(value);

	// select current CV
	io_write(0x0d,dmux);

	// deselect it
	io_write(0x0d,0xff);
}

inline void synth_setCV32Sat_FastPath(p600CV_t cv,int32_t value)
{
	value=MAX(value,0);
	value=MIN(value,UINT16_MAX);
	
	synth_setCV_FastPath(cv,value);
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
	updateCV(cv,synth.cvs[cv]);
}

void synth_init()
{
	memset(&synth,0,sizeof(synth));
}

void synth_update()
{
	uint8_t i;

	for(i=0;i<SYNTH_CV_COUNT;++i)
		updateCV(i,synth.cvs[i]);

	updateGates();
}

