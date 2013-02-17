////////////////////////////////////////////////////////////////////////////////
// Synthesizer CVs/Gates
////////////////////////////////////////////////////////////////////////////////

#include "synth.h"
#include "dac.h"

#define SYNTH_CV_COUNT 32

static struct
{
	uint16_t cvs[SYNTH_CV_COUNT];
	uint8_t gateBits;
} synth;

static inline void updateGates(void)
{
	io_write(0x0b,synth.gateBits);
}

static inline void updateCV(p600CV_t cv)
{
	uint8_t dmux;
	uint16_t cvv;
	
	dmux=(cv&0x07)|(~(0x08<<(cv>>3))&0xf8);
	cvv=synth.cvs[cv];

	HW_ACCESS
	{
		dac_write(cvv);

		// select current CV
		io_write(0x0d,dmux);

		// let S&H get correct voltage
		CYCLE_WAIT(2);

		// unselect
		io_write(0x0d,0xff);
	}
}

void inline synth_setCV(p600CV_t cv,uint16_t value, int8_t immediate)
{
	synth.cvs[cv]=value;
	
	if(immediate)
		updateCV(cv);
}

void inline synth_setGate(p600Gate_t gate,int8_t on)
{
	uint8_t mask=1<<gate;
	
	synth.gateBits&=~mask;
	if (on) synth.gateBits|=mask;
	
	updateGates();
}

void synth_init()
{
	memset(&synth,0,sizeof(synth));
}

void synth_update()
{
	uint8_t i;

	for(i=0;i<SYNTH_CV_COUNT;++i)
		updateCV(i);

	updateGates();
}

