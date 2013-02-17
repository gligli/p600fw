////////////////////////////////////////////////////////////////////////////////
// Tunes a CV using the 8253 timer, by counting audio cycles
////////////////////////////////////////////////////////////////////////////////

#include "tuner.h"
#include "synth.h"
#include "display.h"

#define TUNER_CV_COUNT 18

#define FF_P	0x01 // active low
#define CNTR_EN 0x02
#define FF_D	0x08
#define FF_CL	0x10 // active low

#define STATUS_TIMEOUT 1000

#define TUNER_TICK 2000000.0f
#define TUNER_LOWEST_HERTZ 16.351875f

#define TUNER_ACCEPTABLE_ERROR 4.0f // 14bit DAC, 16bit values

#define TUNER_SCALE_SLEW_RATE 0.5f // higher is faster, until it overshoots and becomes slower!
#define TUNER_OFFSET_SLEW_RATE 0.66f // higher is faster, until it overshoots and becomes slower!

#define TUNER_FIL_INIT_OFFSET 10000
#define TUNER_FIL_INIT_SCALE 20
#define TUNER_FIL_SCALE_NTH_C_LO 3
#define TUNER_FIL_SCALE_NTH_C_HI 5
#define TUNER_FIL_OFFSET_NTH_C 3

#define TUNER_OSC_INIT_OFFSET 2000
#define TUNER_OSC_INIT_SCALE 10
#define TUNER_OSC_SCALE_NTH_C_LO 2
#define TUNER_OSC_SCALE_NTH_C_HI 4
#define TUNER_OSC_OFFSET_NTH_C 4

static struct
{
	float scales[TUNER_CV_COUNT];
	float offsets[TUNER_CV_COUNT];
	p600CV_t currentCV;
} tuner;

static NOINLINE void whileTuning(void)
{
	if(tuner.currentCV<pcOsc1B)
		sevenSeg_setAscii('A','1'+tuner.currentCV-pcOsc1A);
	else if(tuner.currentCV<pcFil1)
		sevenSeg_setAscii('B','1'+tuner.currentCV-pcOsc1B);
	else
		sevenSeg_setAscii('F','1'+tuner.currentCV-pcFil1);
	
	display_update();
	synth_update();
	MDELAY(0.5);
}

static void i8253Write(uint8_t a,uint8_t v)
{
	io_write(a,v);
	CYCLE_WAIT(8);
}	

static uint8_t i8253Read(uint8_t a)
{
	CYCLE_WAIT(8);
	return io_read(a);
}

static uint8_t ff_state=0;
static uint8_t ff_step=0;
	
static NOINLINE void ffMask(uint8_t set,uint8_t clear)
{
	ff_state|=set;
	ff_state&=~clear;
	
	io_write(0x0e,ff_state);
	CYCLE_WAIT(4);
	
	++ff_step;
}

static NOINLINE void ffWaitStatus(uint8_t status)
{
	uint8_t s;
	uint16_t timeout=STATUS_TIMEOUT;

	do{
		whileTuning();
		s=io_read(0x9);
		--timeout;
	}while(((s>>1)&0x01)!=status && timeout);


	if (!timeout)
	{
		print("bad flip flop status : ");
		phex(ff_step);
		phex(s);
		print("\n");
	}
}

static NOINLINE uint16_t measureAudioPeriod(uint8_t periods) // in 2Mhz ticks
{
	uint16_t c;
	
	// prepare flip flop
	
	ff_state=0;
	ff_step=0;
	ffMask(FF_P|FF_CL,FF_D|CNTR_EN);
	
	// prepare 8253
	
		// ch1 load 0
	i8253Write(0x1,0x00);
	i8253Write(0x1,0x00);
	
		// ch2 load 1
	i8253Write(0x2,0x01);
	i8253Write(0x2,0x00);
	
	// flip flop stuff //TODO: EXPLAIN
		
	ffMask(CNTR_EN,0);
		
	while(periods)
	{
		ffMask(0,FF_P);
		ffWaitStatus(0); // check

		whileTuning();
		
		ffMask(FF_P,0);
		ffWaitStatus(1); // wait 

		whileTuning();

		ffMask(FF_D,0);
		ffWaitStatus(0); // wait

		ffMask(0,FF_CL);
		ffWaitStatus(1); // check

		whileTuning();
		
		ffMask(FF_CL,0);
		ffWaitStatus(0); // wait

		whileTuning();

		ffMask(0,FF_D);
		ffWaitStatus(1); // wait
		
		// reload fake clock
		
		ffMask(0,CNTR_EN);
		ffMask(CNTR_EN,0);
		
		--periods;
	}
	
	// read total counter
	
	c=i8253Read(0x1);
	c|=i8253Read(0x1)<<8;
	
	return UINT16_MAX-c;
}

static NOINLINE void tuneOffset(p600CV_t cv,uint8_t nthC)
{
	uint16_t cvv;
	double p,f,tgtf,rcv,tgtcv,newOffset;
	
	tgtf=TUNER_LOWEST_HERTZ*(1<<nthC);
	
	do
	{
		cvv=tuner_computeCVFromNote(12*nthC,cv);

		synth_setCV(cv,cvv,0);
		p=measureAudioPeriod(1);
		
		f=TUNER_TICK/p;
		
		rcv=tuner_computeCVFromFrequency(f,cv);
		tgtcv=tuner_computeCVFromFrequency(tgtf,cv);
		
		newOffset=powf(tgtcv/rcv,TUNER_OFFSET_SLEW_RATE)*(float)tuner.offsets[cv];

#ifdef DEBUG		
		print("cv ");
		phex16(cv);
		print(" per ");
		phex16(p);
		print(" freq ");
		phex16(f);
		print(" off ");
		phex16(tuner.offsets[cv]);
		print(" ");
		phex16(newOffset);
		print("\n");
#endif
		
		tuner.offsets[cv]=newOffset;
	}
	while(fabsf(p-TUNER_TICK/tgtf)>TUNER_ACCEPTABLE_ERROR);

}

static NOINLINE void tuneScale(p600CV_t cv,uint8_t nthCLo,uint8_t nthCHi)
{
	uint16_t cvl,cvh;
	float pl,ph,fl,fh,rcvl,rcvh,newScale;
	
	do
	{
		cvl=tuner_computeCVFromNote(12*nthCLo,cv);
		cvh=tuner_computeCVFromNote(12*nthCHi,cv);

		synth_setCV(cv,cvl,0);
		pl=measureAudioPeriod(1);

		synth_setCV(cv,cvh,0);
		ph=measureAudioPeriod(1<<(nthCHi-nthCLo));
		
		fl=TUNER_TICK/pl;
		fh=TUNER_TICK/ph;
		
		rcvl=tuner_computeCVFromFrequency(fl,cv);
		rcvh=tuner_computeCVFromFrequency(fh,cv);
		
		newScale=powf(rcvl/rcvh,TUNER_SCALE_SLEW_RATE)*(float)tuner.scales[cv]; // higher cv too high -> rcvl/rcvh<1 -> lower scale
		
#ifdef DEBUG		
		print("cvl ");
		phex16(cvl);
		print(" ");
		phex16(cvh);
		print(" per ");
		phex16(pl);
		print(" ");
		phex16(ph);
		print(" freq ");
		phex16(fl);
		print(" ");
		phex16(fh);
		print(" scl ");
		phex16(tuner.scales[cv]);
		print(" ");
		phex16(newScale);
		print("\n");
#endif
		
		tuner.scales[cv]=newScale;
	}
	while(fabsf(ph-pl)>TUNER_ACCEPTABLE_ERROR);
}

static NOINLINE void tuneCV(p600CV_t oscCV, p600CV_t ampCV)
{
#ifdef DEBUG		
	print("\ntuning ");phex(oscCV);print("\n");
#endif
	int8_t isOsc,nthC;
	
	// init (should be ideal values)
	
	tuner.currentCV=oscCV;
	
	isOsc=(oscCV<pcFil1);
	tuner.offsets[oscCV]=isOsc?TUNER_OSC_INIT_OFFSET:TUNER_FIL_INIT_OFFSET;
	tuner.scales[oscCV]=UINT16_MAX/(isOsc?TUNER_OSC_INIT_SCALE:TUNER_FIL_INIT_SCALE);
	nthC=isOsc?TUNER_OSC_OFFSET_NTH_C:TUNER_FIL_OFFSET_NTH_C;
	
	// open VCA

	synth_setCV(ampCV,UINT16_MAX,1);

	// tune
	
	tuneScale(oscCV,isOsc?TUNER_OSC_SCALE_NTH_C_LO:TUNER_FIL_SCALE_NTH_C_LO,isOsc?TUNER_OSC_SCALE_NTH_C_HI:TUNER_FIL_SCALE_NTH_C_HI);
	tuneOffset(oscCV,nthC);

	// close VCA

	synth_setCV(ampCV,0,1);
}

uint16_t NOINLINE tuner_computeCVFromFrequency(float frequency,p600CV_t cv)
{
	float value;

	value=log2f(frequency/TUNER_LOWEST_HERTZ)*tuner.scales[cv]+tuner.offsets[cv];
	
	return MIN(MAX(value,0.0f),65535.0f);
}

uint16_t NOINLINE tuner_computeCVFromNote(uint8_t note,p600CV_t cv)
{
	float value;
	
	value=((float)note/12.0f)*tuner.scales[cv]+tuner.offsets[cv];
	
	return MIN(MAX(value,0.0f),65535.0f);
}

void tuner_tuneSynth(void)
{
	int8_t i;
	
	HW_ACCESS
	{
		// init synth
		
		display_clear();
		led_set(plTune,1,0);
		
		synth_setCV(pcMVol,25000,0);

		synth_setGate(pgASaw,0);
		synth_setGate(pgATri,1);
		synth_setGate(pgBSaw,0);
		synth_setGate(pgBTri,1);
		synth_setGate(pgPModFA,0);
		synth_setGate(pgPModFil,0);
		synth_setGate(pgSync,0);

		synth_setCV(pcRes,0,0);
		synth_setCV(pcAPW,0,0);
		synth_setCV(pcBPW,0,0);
		synth_setCV(pcPModOscB,0,0);
		synth_setCV(pcExtFil,0,0);
		
		// init 8253
			// ch 0, mode 0, access 2 bytes, binary count
		i8253Write(0x3,0b00110000); 
			// ch 1, mode 0, access 2 bytes, binary count
		i8253Write(0x3,0b01110000); 
			// ch 2, mode 1, access 2 bytes, binary count
		i8253Write(0x3,0b10110010); 

		// tune oscs
			
			// init
		
		synth_setCV(pcRes,0,0);
		for(i=0;i<P600_VOICE_COUNT;++i)
			synth_setCV(pcFil1+i,UINT16_MAX,0);
	
			// A oscs

		synth_setCV(pcVolA,UINT16_MAX,0);
		synth_setCV(pcVolB,0,0);

		for(i=0;i<P600_VOICE_COUNT;++i)
			tuneCV(pcOsc1A+i,pcAmp1+i);

			// B oscs

		synth_setCV(pcVolA,0,0);
		synth_setCV(pcVolB,UINT16_MAX,0);

		for(i=0;i<P600_VOICE_COUNT;++i)
			tuneCV(pcOsc1B+i,pcAmp1+i);

		// tune filters
			
			// init
		
		synth_setCV(pcVolA,0,0);
		synth_setCV(pcVolB,0,0);
		synth_setCV(pcRes,UINT16_MAX,0);

		for(i=0;i<P600_VOICE_COUNT;++i)
			synth_setCV(pcFil1+i,0,0);
	
			// filters
		
		for(i=0;i<P600_VOICE_COUNT;++i)
			tuneCV(pcFil1+i,pcAmp1+i);

		// finish
		
		display_clear();
	}
}
