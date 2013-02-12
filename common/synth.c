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

static void updateGates(void)
{
	io_write(0x0b,synth.gateBits);
}

static void updateCV(p600CV_t cv)
{
	uint8_t dmux;
	
	int_clear();
	
	// write DAC
	dac_write(synth.cvs[cv]);

	// select current CV
	dmux=(cv&0x07)|(~(0x08<<(cv>>3))&0xf8);
	io_write(0x0d,dmux);

	// let S&H get correct voltage
	wait(8);

	// unselect
	io_write(0x0d,0xff);
	
	int_set();
}

void synth_setCV(p600CV_t cv,uint16_t value, int8_t immediate)
{
	synth.cvs[cv]=value;
	
	if(immediate)
		updateCV(cv);
}

void synth_setGate(p600Gate_t gate,int8_t on, int8_t immediate)
{
	uint8_t mask=1<<gate;
	
	synth.gateBits&=~mask;
	if (on) synth.gateBits|=mask;
	
	if(immediate)
		updateGates();
}

void synth_init()
{
	memset(&synth,0,sizeof(synth));
}

void synth_update()
{
	uint8_t i;

	// update CVs
	for(i=0;i<SYNTH_CV_COUNT;++i)
		updateCV(i);
		
	// update gates
	updateGates();
}

