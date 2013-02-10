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

void synth_setCV(p600CV_t cv,uint16_t value)
{
	synth.cvs[cv]=value;
}

void synth_setGate(p600Gate_t gate,int on)
{
	uint8_t mask=1<<gate;
	
	synth.gateBits&=~mask;
	if (on) synth.gateBits|=mask;
}

void synth_init()
{
	memset(&synth,0,sizeof(synth));
}

void synth_update()
{
	uint8_t i;
	uint8_t dmux;

	for(i=0;i<SYNTH_CV_COUNT;++i)
	{
		// write DAC
		dac_write(synth.cvs[i]);

		// select current CV
		dmux=(i&0x07)|(~(0x08<<(i>>3))&0xf8);
		io_write(0x0d,dmux);

		// let S&H get correct voltage
		wait(8);

		// unselect
		io_write(0x0d,0xff);
	}
	
	// update gates
	io_write(0x0b,synth.gateBits);
}

