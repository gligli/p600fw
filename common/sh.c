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
	uint16_t sent[SH_CV_COUNT]; // last value actually sent to each S&H channel
	uint8_t tick; // incremented once per timer tick
} sh;

// Fast path S&H updates are skipped when the value is unchanged, but every channel is still re-sent
// at least once every SH_REFRESH_PERIOD timer ticks (power of 2, 4 ticks = 2 ms) so the hold capacitors stay refreshed.
#define SH_REFRESH_PERIOD 4

void sh_nextTick(void)
{
	sh.tick++;
}

static inline void updateGates(void)
{
	BLOCK_INT
	{
		io_write(CSO3,sh.gateBits);
	}
}

static inline void updateCV(p600CV_t cv, uint16_t cvv)
{
	uint8_t dmux1,dmux2;
	
	sh.sent[cv]=cvv; // keep the cache coherent when the slow path (tuner, sh_update) sends a value
	
	dmux1=(cv&0x07)|0xf8;
	dmux2=dmux1&~(0x08<<(cv>>3));
	
	BLOCK_INT
	{
		dac_write(cvv);
	
		// prepare S&H
		io_write(CS05,dmux1);
		
		// for DAC rise time
		CYCLE_WAIT(4);
		//CYCLE_WAIT(1);

		// select current CV
		io_write(CS05,dmux2);

		// 2 us to let S&H get very precise voltage, some P600s need it apparently
		CYCLE_WAIT(8);
		//CYCLE_WAIT(1);

		// deselect it
		io_write(CS05,dmux1);

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
		sh.cvs[cv]=value;
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

FORCEINLINE void sh_setCV_FastPath(p600CV_t cv,uint16_t value)
{
	uint8_t dmux1,dmux2;

	// nothing changed and this channel is not due for its periodic refresh: skip the 5 bus cycles
	if (sh.sent[cv]==value && ((uint8_t)(sh.tick+cv)&(SH_REFRESH_PERIOD-1)))
		return;
	sh.sent[cv]=value;
	
	dmux1=(cv&0x07)|0xf8;
	dmux2=dmux1&~(0x08<<(cv>>3));
	
	dac_write(value);

	// prepare S&H
	io_write(CS05,dmux1);

	// for DAC rise time
	CYCLE_WAIT(1);

	// select current CV
	io_write(CS05,dmux2);

	// let S&H get very precise voltage (cf tohk issue)
	CYCLE_WAIT(1);
	
	// deselect it
	io_write(CS05,dmux1);

    // let analog hardware stabilize
    // CYCLE_WAIT(1);

}

FORCEINLINE void sh_setCV32Sat_FastPath(p600CV_t cv,int32_t value)
{
	if(value<0)
		value=0;
	else if (value>UINT16_MAX)
		value=UINT16_MAX;

	sh_setCV_FastPath(cv,value);
}

inline void sh_setGate(p600Gate_t gate,int8_t on)
{
	uint8_t mask=1<<gate;
	
	sh.gateBits&=~mask;
	if (on) sh.gateBits|=mask;
	
	updateGates();
}

void sh_init()
{
	memset(&sh,0,sizeof(sh));
}

void sh_update()
{
	uint8_t i;

	for(i=0;i<SH_CV_COUNT;++i)
		updateCV(i,sh.cvs[i]);

	updateGates();
}

