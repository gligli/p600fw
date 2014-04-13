////////////////////////////////////////////////////////////////////////////////
// Synthesizer sample and holds / gates
////////////////////////////////////////////////////////////////////////////////

#include "sh.h"
#include "dac.h"

#define SH_CV_COUNT 32

static struct
{
	uint32_t immediateBits;
	uint16_t cvs[SH_CV_COUNT];
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
		
		// for DAC rise time
		CYCLE_WAIT(4);

		// select current CV
		io_write(0x0d,dmux);

		// 2 us to let S&H get very precise voltage, some P600s need it apparently
		CYCLE_WAIT(8);
		
		// deselect it
		io_write(0x0d,0xff);

		// 2 more us to let analog hardware stabilize
		CYCLE_WAIT(8);
	}
}

inline void sh_setCV(p600CV_t cv,uint16_t value, uint8_t flags)
{
	if(flags&SH_FLAG_IMMEDIATE)
	{
		updateCV(cv,value);
	}
	else
	{
		synth.cvs[cv]=value;
	}
}

inline void sh_setCV32Sat(p600CV_t cv,int32_t value, uint8_t flags)
{
	if(value<0)
		value=0;
	else if (value>UINT16_MAX)
		value=UINT16_MAX;

	sh_setCV(cv,value,flags);
}

inline void sh_setCV_FastPath(p600CV_t cv,uint16_t value)
{
	uint8_t dmux;
	
	dmux=(cv&0x07)|(~(0x08<<(cv>>3))&0xf8);

	dac_write(value);

	// for DAC rise time (cf tohk issue)
	CYCLE_WAIT(1)
			
	// select current CV
	io_write(0x0d,dmux);

	// let S&H get very precise voltage (cf tohk issue)
	CYCLE_WAIT(1)
	
	// deselect it
	io_write(0x0d,0xff);
}

inline void sh_setCV32Sat_FastPath(p600CV_t cv,int32_t value)
{
	value=MAX(value,0);
	value=MIN(value,UINT16_MAX);
	
	sh_setCV_FastPath(cv,value);
}

inline void sh_setGate(p600Gate_t gate,int8_t on)
{
	uint8_t mask=1<<gate;
	
	synth.gateBits&=~mask;
	if (on) synth.gateBits|=mask;
	
	updateGates();
}

void sh_init()
{
	memset(&synth,0,sizeof(synth));
}

void sh_update()
{
	uint8_t i;

	for(i=0;i<SH_CV_COUNT;++i)
		updateCV(i,synth.cvs[i]);

	updateGates();
}

