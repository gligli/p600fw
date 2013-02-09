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

	uint32_t cvsOk;
	uint8_t gatesOk;
} synth;

void synth_setCV(p600CV_t cv,uint16_t value)
{
	if(synth.cvs[cv]!=value)
	{
		uint32_t mask=((uint32_t)1)<<cv;
		synth.cvs[cv]=value;
		synth.cvsOk&=~mask;
	}
}

void synth_setGate(p600Gate_t gate,int on)
{
	uint8_t mask=1<<gate;
	
	synth.gateBits&=~mask;
	if (on) synth.gateBits|=mask;
		
	synth.gatesOk&=~mask;
}

void synth_invalidate()
{
	synth.cvsOk=0;
	synth.gatesOk=0;
}

void synth_init()
{
	memset(&synth,0,sizeof(synth));
}

void synth_update()
{
	if(synth.cvsOk!=0xffffffff)
	{
		uint8_t i;
		uint32_t mask;
		uint8_t dmux;

		for(i=0;i<SYNTH_CV_COUNT;++i)
		{
			mask=((uint32_t)1)<<i;

			if (!(mask&synth.cvsOk))
			{
				dmux=(i&0x07)|(~(0x08<<(i>>3))&0xf8);
				
				io_write(0x0d,dmux);
				dac_write(synth.cvs[i]);
			}
		}
		
		synth.cvsOk=0xffffffff;
	}
	
	if (synth.gatesOk!=0xff)
	{
		io_write(0x0b,synth.gateBits);
		synth.gatesOk=0xff;
	}
	
}

