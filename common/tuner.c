////////////////////////////////////////////////////////////////////////////////
// Tunes a CV using the 8253 timer, by counting audio cycles
////////////////////////////////////////////////////////////////////////////////

#include "tuner.h"
#include "synth.h"

#define TUNER_CV_COUNT 18

#define FF_P	0x01 // active low
#define CNTR_EN 0x02
#define FF_D	0x08
#define FF_CL	0x10 // active low

#define STATUS_TIMEOUT UINT16_MAX

#define TUNER_ACCEPTABLE_ERROR 1
#define TUNER_TICK 2000000.0
#define TUNER_OFFSET_HERTZ 261.63
#define TUNER_OFFSET_TICKS (TUNER_TICK/TUNER_OFFSET_HERTZ)

#define TUNER_FIL_INIT_OFFSET 20000
#define TUNER_FIL_INIT_SCALE 20
#define TUNER_FIL_SERVO_RATIO 0.05
#define TUNER_FIL_SCALE_NTH_C_LO 2
#define TUNER_FIL_SCALE_NTH_C_HI 4
#define TUNER_FIL_OFFSET_NTH_C 5

#define TUNER_OSC_INIT_OFFSET 50
#define TUNER_OSC_INIT_SCALE 10
#define TUNER_OSC_SERVO_RATIO 0.05
#define TUNER_OSC_SCALE_NTH_C_LO 2
#define TUNER_OSC_SCALE_NTH_C_HI 4
#define TUNER_OSC_OFFSET_NTH_C 5

static struct
{
	uint16_t scales[TUNER_CV_COUNT];
	uint16_t offsets[TUNER_CV_COUNT];
} tuner;

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

static NOINLINE uint16_t measureAudioPeriod(uint8_t periods, p600CV_t cv, uint16_t cvv) // in 2Mhz ticks
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
		synth_setCV(cv,cvv,0);
		synth_update();		
		MDELAY(2);
		
		ffMask(0,FF_P);
		ffWaitStatus(0); // check

		ffMask(FF_P,0);
		ffWaitStatus(1); // wait 

		ffMask(FF_D,0);
		ffWaitStatus(0); // wait

		synth_setCV(cv,cvv,0);
		synth_update();		
		MDELAY(2);
		
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
	}
	
	// read total counter
	
	c=i8253Read(0x1);
	c|=i8253Read(0x1)<<8;
	
	return UINT16_MAX-c;
}

static NOINLINE void tuneOffset(p600CV_t cv,uint8_t nthC,double servoRatio)
{
	uint16_t newOffset,cvv,p;
	int16_t error;
	double ratio;
	
	do
	{
		cvv=tuner_computeNoteCV(12*nthC,cv);

		p=measureAudioPeriod(1,cv,cvv);
		
		ratio=((p/TUNER_OFFSET_TICKS)-1.0)*servoRatio+1.0; // minimise overshoot

		newOffset=ratio*tuner.offsets[cv];
		
		error=tuner.offsets[cv]-newOffset;

#ifdef DEBUG		
		phex16(p);
		print("\t");
		phex16(newOffset);
		phex16(tuner.offsets[cv]);
		print("\t");
		phex16(error);
		print("\n");
#endif
		
		tuner.offsets[cv]=newOffset;
	}
	while(error<-TUNER_ACCEPTABLE_ERROR || error>TUNER_ACCEPTABLE_ERROR);

}

static NOINLINE void tuneScale(p600CV_t cv,uint8_t nthCLo,uint8_t nthCHi,double servoRatio)
{
	uint16_t newScale,cvl,cvh,pl,ph;
	int16_t error;
	double ratio;
	
	do
	{
		cvl=tuner_computeNoteCV(12*nthCLo,cv);
		cvh=tuner_computeNoteCV(12*nthCHi,cv);

		pl=measureAudioPeriod(1,cv,cvl);
		ph=measureAudioPeriod(1<<(nthCHi-nthCLo),cv,cvh);
		
		ratio=((ph/pl)-1.0)*servoRatio+1.0; // minimise overshoot

		newScale=ratio*tuner.scales[cv];
		
		error=tuner.scales[cv]-newScale;
		
#ifdef DEBUG		
		phex16(pl);
		phex16(ph);
		print("\t");
		phex16(newScale);
		phex16(tuner.scales[cv]);
		print("\t");
		phex16(error);
		print("\n");
#endif
		
		tuner.scales[cv]=newScale;
	}
	while(error<-TUNER_ACCEPTABLE_ERROR || error>TUNER_ACCEPTABLE_ERROR);
}

static NOINLINE void tuneCV(p600CV_t oscCV, p600CV_t ampCV)
{
#ifdef DEBUG		
	print("\ntuning ");phex(oscCV);print("\n");
#endif
	int8_t isOsc,nthC;
	double servoRatio;
	
	// init (should be typical values)
	
	isOsc=(oscCV<pcFil1);
	tuner.offsets[oscCV]=isOsc?TUNER_OSC_INIT_OFFSET:TUNER_FIL_INIT_OFFSET;
	tuner.scales[oscCV]=UINT16_MAX/(isOsc?TUNER_OSC_INIT_SCALE:TUNER_FIL_INIT_SCALE);
	servoRatio=isOsc?TUNER_OSC_SERVO_RATIO:TUNER_FIL_SERVO_RATIO;
	nthC=isOsc?TUNER_OSC_OFFSET_NTH_C:TUNER_FIL_OFFSET_NTH_C;
	
	// open VCA

	synth_setCV(ampCV,UINT16_MAX,1);

	// tune
	
	tuneOffset(oscCV,nthC,servoRatio);
	tuneScale(oscCV,isOsc?TUNER_OSC_SCALE_NTH_C_LO:TUNER_FIL_SCALE_NTH_C_LO,isOsc?TUNER_OSC_SCALE_NTH_C_HI:TUNER_FIL_SCALE_NTH_C_HI,servoRatio);
	tuneOffset(oscCV,nthC,servoRatio);

	// close VCA

	synth_setCV(ampCV,0,1);
}

uint16_t NOINLINE tuner_computeNoteCV(uint8_t note,p600CV_t cv)
{
	uint16_t offsetPart,scalePart;
	
	offsetPart=tuner.offsets[cv];
	scalePart=(uint32_t)note*tuner.scales[cv]/12UL;
	
	return SADD16(offsetPart,scalePart);
}

void tuner_tuneSynth(void)
{
	int8_t i;
	
	HW_ACCESS
	{
		// init 8253
			// ch 0, mode 0, access 2 bytes, binary count
		i8253Write(0x3,0b00110000); 
			// ch 1, mode 0, access 2 bytes, binary count
		i8253Write(0x3,0b01110000); 
			// ch 2, mode 1, access 2 bytes, binary count
		i8253Write(0x3,0b10110010); 

		// init
		
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

	}
}
