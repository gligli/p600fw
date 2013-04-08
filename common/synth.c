////////////////////////////////////////////////////////////////////////////////
// Synthesizer CVs/Gates
////////////////////////////////////////////////////////////////////////////////

#include "synth.h"
#include "dac.h"

#define SYNTH_CV_COUNT 32
	
static struct
{
	uint16_t cvs[SYNTH_CV_COUNT];
	p600CV_t refreshedCV;
	uint8_t gatesBits;
} synth;

static inline void updateGates(void)
{
	io_write(0x0b,synth.gatesBits);
}

static inline void updateCV(p600CV_t cv, uint16_t cvv, int8_t wait)
{
	uint8_t dmux;
	
	dmux=(cv&0x07)|(~(0x08<<(cv>>3))&0xf8);

	dac_write(cvv);

	// select current CV
	io_write(0x0d,dmux);

	// 2.5 us to let S&H get very precise voltage, some P600s need it apparently
	if(wait)
		CYCLE_WAIT(10);

	// deselect it
	io_write(0x0d,0xff);

	// 2.5 more us to let analog hardware stabilize
	if(wait)
		CYCLE_WAIT(10);
}

inline void synth_setCV(p600CV_t cv,uint16_t value,int8_t immediate, int8_t store)
{
	if(immediate)
	{
		updateCV(cv,value,0);
	}
	
	if(store)
	{
		synth.cvs[cv]=value;
	}
}

inline void synth_setCV32Sat(p600CV_t cv,int32_t value,int8_t immediate, int8_t store)
{
	if(value<0)
		value=0;
	else if (value>UINT16_MAX)
		value=UINT16_MAX;
	
	synth_setCV(cv,value,immediate,store);
}

inline void synth_setGate(p600Gate_t gate,int8_t on)
{
	BLOCK_INT
	{
		uint8_t mask=1<<gate;

		if(on)
			synth.gatesBits|=mask;
		else
			synth.gatesBits&=~mask;
	}
}

void synth_forceUpdateCV(p600CV_t cv)
{
	BLOCK_INT
	{
		updateCV(cv,synth.cvs[cv],1);
	}
}

void synth_forceUpdateAll(void)
{
	int8_t i;

	BLOCK_INT
	{
		for(i=0;i<SYNTH_CV_COUNT;++i)
			updateCV(i,synth.cvs[i],1);

		updateGates();
	}
}

void synth_init()
{
	memset(&synth,0,sizeof(synth));
	synth.refreshedCV=pcAmp1;
}

void synth_update()
{
	// gates

	updateGates();

	// refresh CVs periodically (to counter S&H drift)

	updateCV(synth.refreshedCV,synth.cvs[synth.refreshedCV],1);

	if(synth.refreshedCV<pcBPW)
		++synth.refreshedCV;
	else
		synth.refreshedCV=pcAmp1;
}

