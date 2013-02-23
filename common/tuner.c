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
#define TUNER_MIDDLE_C_HERTZ 261.63f

#define TUNER_PRECISION 2 // 0-n, higher is preciser but slower

#define TUNER_SCALE_SLEW_RATE 0.33f // higher is faster, until it overshoots and becomes slower!
#define TUNER_OFFSET_SLEW_RATE 0.8f // higher is faster, until it overshoots and becomes slower!

#define TUNER_OSC_LOWEST_HERTZ (TUNER_MIDDLE_C_HERTZ/16)
#define TUNER_OSC_EPSILON 1.0f
#define TUNER_OSC_INIT_OFFSET 5000.0f
#define TUNER_OSC_INIT_SCALE (65536.0f/10.0f)
#define TUNER_OSC_SCALE_NTH_C_LO 3
#define TUNER_OSC_SCALE_NTH_C_HI 6
#define TUNER_OSC_OFFSET_NTH_C 5

#define TUNER_FIL_LOWEST_HERTZ (TUNER_MIDDLE_C_HERTZ/32)
#define TUNER_FIL_EPSILON 2.0f
#define TUNER_FIL_INIT_OFFSET 10000.0f
#define TUNER_FIL_INIT_SCALE (65536.0f/20.0f)
#define TUNER_FIL_SCALE_NTH_C_LO 4
#define TUNER_FIL_SCALE_NTH_C_HI 7
#define TUNER_FIL_OFFSET_NTH_C 6

static struct
{
	float scales[TUNER_CV_COUNT];
	float offsets[TUNER_CV_COUNT];
	p600CV_t currentCV;
} tuner;

static NOINLINE void whileTuning(void)
{
	static uint8_t frc=0;
	
	if((frc&0x0f)==0)
	{
		if(tuner.currentCV<pcOsc1B)
			sevenSeg_setAscii('A','1'+tuner.currentCV-pcOsc1A);
		else if(tuner.currentCV<pcFil1)
			sevenSeg_setAscii('B','1'+tuner.currentCV-pcOsc1B);
		else
			sevenSeg_setAscii('F','1'+tuner.currentCV-pcFil1);

		display_update();
	}
	
	synth_update();
	++frc;
}

static NOINLINE void i8253Write(uint8_t a,uint8_t v)
{
	io_write(a,v);
	CYCLE_WAIT(8);
}	

static NOINLINE uint8_t i8253Read(uint8_t a)
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

static NOINLINE uint16_t getPeriod(void)
{
	uint16_t c;
	
	// read counter, add to result
	c=i8253Read(0x1);
	c|=i8253Read(0x1)<<8;

	// ch1 reload 0
	i8253Write(0x1,0x00);
	i8253Write(0x1,0x00);

	return UINT16_MAX-c;
}

static NOINLINE uint32_t measureAudioPeriod(uint8_t periods) // in 2Mhz ticks
{
	uint32_t res=0;
	
	//
	
	whileTuning();
			
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

		ffMask(FF_P,0);
		ffWaitStatus(1); // wait 

		ffMask(FF_D,0);
		ffWaitStatus(0); // wait

		ffMask(0,FF_CL);
		ffWaitStatus(1); // check

		ffMask(FF_CL,0);
		ffWaitStatus(0); // wait

		ffMask(0,FF_D);
		ffWaitStatus(1); // wait
		
		// reload fake clock
		
		ffMask(0,CNTR_EN);
		ffMask(CNTR_EN,0);
		
		--periods;

		res+=getPeriod();
	}
	
	// read total counter
	
	return res;
}

static NOINLINE void tuneOffset(p600CV_t cv,uint8_t nthC, float epsilon, float lowestFreq)
{
	uint16_t cvv;
	uint32_t p,tgtp;
	float newOffset,error;
	
	tgtp=(uint32_t)(TUNER_TICK/lowestFreq)>>(nthC-TUNER_PRECISION);
	
	do
	{
		cvv=tuner_computeCVFromNote(12*nthC,cv);

		synth_setCV(cv,cvv,0);
		p=measureAudioPeriod(1<<TUNER_PRECISION);
		
		newOffset=(float)tuner.offsets[cv]*powf((float)p/(float)tgtp,TUNER_OFFSET_SLEW_RATE);

		error=fabsf(tuner.offsets[cv]-newOffset);
		
#ifdef DEBUG		
		print("cv ");
		phex16(cvv);
		print(" per ");
		phex16(p>>16);
		phex16(p);
		print(" ");
		phex16(tgtp>>16);
		phex16(tgtp);
		print(" off ");
		phex16(tuner.offsets[cv]);
		print(" ");
		phex16(newOffset);
		print("\n");
#endif
		
		tuner.offsets[cv]=newOffset;
	}
	while(error>epsilon);

}

static NOINLINE void tuneScale(p600CV_t cv,uint8_t nthCLo,uint8_t nthCHi, float epsilon)
{
	uint16_t cvl,cvh;
	uint32_t pl,ph;
	float newScale,error;
	
	do
	{
		cvl=tuner_computeCVFromNote(12*nthCLo,cv);
		cvh=tuner_computeCVFromNote(12*nthCHi,cv);

		synth_setCV(cv,cvl,0);
		pl=measureAudioPeriod(1<<TUNER_PRECISION);

		synth_setCV(cv,cvh,0);
		ph=measureAudioPeriod(1<<(nthCHi-nthCLo+TUNER_PRECISION));
		
		newScale=(float)tuner.scales[cv]*powf((float)ph/(float)pl,TUNER_SCALE_SLEW_RATE);
		
		error=fabsf(tuner.scales[cv]-newScale);
		
#ifdef DEBUG		
		print("cv ");
		phex16(cvl);
		print(" ");
		phex16(cvh);
		print(" per ");
		phex16(pl>>16);
		phex16(pl);
		print(" ");
		phex16(ph>>16);
		phex16(ph);
		print(" scl ");
		phex16(tuner.scales[cv]);
		print(" ");
		phex16(newScale);
		print("\n");
#endif
		
		tuner.scales[cv]=newScale;
	}
	while(error>epsilon);
}

static NOINLINE void tuneCV(p600CV_t oscCV, p600CV_t ampCV)
{
#ifdef DEBUG		
	print("\ntuning ");phex(oscCV);print("\n");
#endif
	int8_t isOsc;
	
	// init
	
	tuner.currentCV=oscCV;
	isOsc=(oscCV<pcFil1);

	// open VCA

	synth_setCV(ampCV,UINT16_MAX,0);
	synth_update();

	// tune

	if (isOsc)
	{
		tuneScale(oscCV,TUNER_OSC_SCALE_NTH_C_LO,TUNER_OSC_SCALE_NTH_C_HI,TUNER_OSC_EPSILON);
		tuneOffset(oscCV,TUNER_OSC_OFFSET_NTH_C,TUNER_OSC_EPSILON,TUNER_OSC_LOWEST_HERTZ);
	}
	else
	{
		tuneScale(oscCV,TUNER_FIL_SCALE_NTH_C_LO,TUNER_FIL_SCALE_NTH_C_HI,TUNER_FIL_EPSILON);
		tuneOffset(oscCV,TUNER_FIL_OFFSET_NTH_C,TUNER_FIL_EPSILON,TUNER_FIL_LOWEST_HERTZ);
	}
	
	// close VCA

	synth_setCV(ampCV,0,0);
	synth_update();
}

uint16_t tuner_computeCVFromFrequency(float frequency,p600CV_t cv)
{
	float value,lowestFreq;
	
	lowestFreq=(cv<pcFil1)?TUNER_OSC_LOWEST_HERTZ:TUNER_FIL_LOWEST_HERTZ;

	value=log2f(frequency/lowestFreq)*tuner.scales[cv]+tuner.offsets[cv];
	
	return MIN(MAX(value,0.0f),65535.0f);
}

uint16_t tuner_computeCVFromNote(uint8_t note,p600CV_t cv)
{
	float value;
	
	value=((float)note/12.0f)*tuner.scales[cv]+tuner.offsets[cv];
	
	return MIN(MAX(value,0.0f),65535.0f);
}

void tuner_init(void)
{
	int8_t i;
	
	memset(&tuner,0,sizeof(tuner));
	
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		tuner.offsets[i+pcOsc1A]=TUNER_OSC_INIT_OFFSET;
		tuner.offsets[i+pcOsc1B]=TUNER_OSC_INIT_OFFSET;
		tuner.offsets[i+pcFil1]=TUNER_FIL_INIT_OFFSET;

		tuner.scales[i+pcOsc1A]=TUNER_OSC_INIT_SCALE;
		tuner.scales[i+pcOsc1B]=TUNER_OSC_INIT_SCALE;
		tuner.scales[i+pcFil1]=TUNER_FIL_INIT_SCALE;
	}
}

void tuner_tuneSynth(void)
{
	int8_t i;
	
	BLOCK_INT
	{
		// init synth
		
		display_clear();
		led_set(plTune,1,0);
		
#ifdef DEBUG		
		synth_setCV(pcMVol,20000,0);
#else
		synth_setCV(pcMVol,0,0);
#endif

		synth_setGate(pgASaw,0);
		synth_setGate(pgATri,1);
		synth_setGate(pgBSaw,0);
		synth_setGate(pgBTri,1);
		synth_setGate(pgPModFA,0);
		synth_setGate(pgPModFil,0);
		synth_setGate(pgSync,0);

		synth_setCV(pcResonance,0,0);
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
		
		synth_setCV(pcResonance,0,0);
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
		synth_setCV(pcResonance,UINT16_MAX,0);

		for(i=0;i<P600_VOICE_COUNT;++i)
			synth_setCV(pcFil1+i,0,0);
	
			// filters
		
		for(i=0;i<P600_VOICE_COUNT;++i)
			tuneCV(pcFil1+i,pcAmp1+i);

		// finish
		
		synth_setCV(pcResonance,0,0);
		for(i=0;i<P600_VOICE_COUNT;++i)
			synth_setCV(pcAmp1+i,0,0);
		
		synth_update();

		display_clear();
	}
}
