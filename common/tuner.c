////////////////////////////////////////////////////////////////////////////////
// Tunes a CV using the 8253 timer, by counting audio cycles
////////////////////////////////////////////////////////////////////////////////

#include "tuner.h"
#include "synth.h"
#include "display.h"

#define TUNER_CV_COUNT (pcFil6-pcOsc1A+1)
#define TUNER_OCTAVE_COUNT 12

#define FF_P	0x01 // active low
#define CNTR_EN 0x02
#define FF_D	0x08
#define FF_CL	0x10 // active low

#define STATUS_TIMEOUT 30000

#define TUNER_TICK 2000000.0
#define TUNER_MIDDLE_C_HERTZ 261.63

#define TUNER_VCA_LEVEL 32768

#define TUNER_OSC_LOWEST_HERTZ (TUNER_MIDDLE_C_HERTZ/16)
#define TUNER_OSC_INIT_OFFSET 5000.0
#define TUNER_OSC_INIT_SCALE (65536.0/11.0)
#define TUNER_OSC_PRECISION -2 // higher is preciser but slower
#define TUNER_OSC_NTH_C_LO 3
#define TUNER_OSC_NTH_C_HI 6

#define TUNER_FIL_LOWEST_HERTZ (TUNER_MIDDLE_C_HERTZ/32)
#define TUNER_FIL_INIT_OFFSET 10000.0
#define TUNER_FIL_INIT_SCALE (65536.0/22.0)
#define TUNER_FIL_PRECISION -3 // higher is preciser but slower
#define TUNER_FIL_NTH_C_LO 4
#define TUNER_FIL_NTH_C_HI 6

static struct
{
	uint16_t tunes[TUNER_OCTAVE_COUNT][TUNER_CV_COUNT];
	p600CV_t currentCV;
} tuner;

static NOINLINE void whileTuning(void)
{
	static uint8_t frc=0;
	
	if((frc&0x7f)==0)
	{
		// display current osc
		if(tuner.currentCV<pcOsc1B)
			sevenSeg_setAscii('A','1'+tuner.currentCV-pcOsc1A);
		else if(tuner.currentCV<pcFil1)
			sevenSeg_setAscii('B','1'+tuner.currentCV-pcOsc1B);
		else
			sevenSeg_setAscii('F','1'+tuner.currentCV-pcFil1);

		display_update(1);

		// full update once in a while
		synth_update();
	}
	
	synth_updateCV(tuner.currentCV);
	++frc;
}

static NOINLINE void i8253Write(uint8_t a,uint8_t v)
{
	io_write(a,v);
	CYCLE_WAIT(4);
}	

static NOINLINE uint8_t i8253Read(uint8_t a)
{
	CYCLE_WAIT(4);
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

	return UINT16_MAX-c+10; // 10 -> helps for uppper octaves tuning, not yet sure why...
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

static NOINLINE void tuneOffset(p600CV_t cv,uint8_t nthC, uint8_t lowestNote, int8_t precision, double lowestFreq)
{
	int8_t i,relPrec;
	uint16_t estimate,bit;
	double p,tgtp;
	
	tgtp=TUNER_TICK/(lowestFreq*pow(2.0,nthC));
	
	estimate=UINT16_MAX;
	bit=0x8000;
	
	relPrec=precision+nthC;
	
	for(i=0;i<=14;++i) // 14bit dac
	{
		if(estimate>tuner_computeCVFromNote(lowestNote,cv))
		{
			synth_setCV(cv,estimate,0,0);
			p=(double)measureAudioPeriod(1<<relPrec)*pow(2.0,-relPrec);
		}
		else
		{
			p=DBL_MAX;
		}
		
		// adjust estimate
		if (p>tgtp)
			estimate+=bit;
		else
			estimate-=bit;

		// on to finer changes
		bit>>=1;
		
	}

	tuner.tunes[nthC][cv]=estimate;

#ifdef DEBUG		
	print("cv ");
	phex16(estimate);
	print(" per ");
	phex16(p);
	print(" ");
	phex16(tgtp);
	print("\n");
#endif
}

static NOINLINE void tuneCV(p600CV_t oscCV, p600CV_t ampCV)
{
#ifdef DEBUG		
	print("\ntuning ");phex(oscCV);print("\n");
#endif
	int8_t isOsc,i;
	
	// init
	
	tuner.currentCV=oscCV;
	isOsc=(oscCV<pcFil1);

	// open VCA

	synth_setCV(ampCV,TUNER_VCA_LEVEL,0,0);
	
	// done many times, to ensure all CVs are at correct voltage
	
	for(i=0;i<100;++i)
		synth_update();

	// tune

	if (isOsc)
	{
		for(i=TUNER_OSC_NTH_C_LO;i<=TUNER_OSC_NTH_C_HI;++i)
			tuneOffset(oscCV,i,12*(TUNER_OSC_NTH_C_LO-2),TUNER_OSC_PRECISION,TUNER_OSC_LOWEST_HERTZ);

		// extrapolate for octaves that aren't directly tunable
		
		for(i=TUNER_OSC_NTH_C_LO-1;i>=0;--i)
			tuner.tunes[i][oscCV]=(uint32_t)2*tuner.tunes[i+1][oscCV]-tuner.tunes[i+2][oscCV];

		for(i=TUNER_OSC_NTH_C_HI+1;i<TUNER_OCTAVE_COUNT;++i)
			tuner.tunes[i][oscCV]=(uint32_t)2*tuner.tunes[i-1][oscCV]-tuner.tunes[i-2][oscCV];
	}
	else
	{
		for(i=TUNER_FIL_NTH_C_LO;i<=TUNER_FIL_NTH_C_HI;++i)
			tuneOffset(oscCV,i,12*(TUNER_FIL_NTH_C_LO-1),TUNER_FIL_PRECISION,TUNER_FIL_LOWEST_HERTZ);

		for(i=TUNER_FIL_NTH_C_LO-1;i>=0;--i)
			tuner.tunes[i][oscCV]=(uint32_t)2*tuner.tunes[i+1][oscCV]-tuner.tunes[i+2][oscCV];

		for(i=TUNER_FIL_NTH_C_HI+1;i<TUNER_OCTAVE_COUNT;++i)
			tuner.tunes[i][oscCV]=(uint32_t)2*tuner.tunes[i-1][oscCV]-tuner.tunes[i-2][oscCV];
	}
	
	// close VCA

	synth_setCV(ampCV,0,0,0);
	synth_update();
}

uint16_t tuner_computeCVFromNote(uint8_t note,p600CV_t cv)
{
	double value,semiTone;
	uint8_t loOct,hiOct;
	uint16_t cvv;
	
	
	loOct=note/12;
	hiOct=MIN(loOct+1,TUNER_OCTAVE_COUNT-1);
	semiTone=(note%12)/12.0f;
	
	value=tuner.tunes[loOct][cv]+(tuner.tunes[hiOct][cv]-tuner.tunes[loOct][cv])*semiTone;
	cvv=round(value);
	
	return cvv;
}

void tuner_init(void)
{
	int8_t i,j;
	
	memset(&tuner,0,sizeof(tuner));
	
	for(j=0;j<TUNER_OCTAVE_COUNT;++j)
		for(i=0;i<P600_VOICE_COUNT;++i)
		{
			tuner.tunes[j][i+pcOsc1A]=TUNER_OSC_INIT_OFFSET+j*TUNER_OSC_INIT_SCALE;
			tuner.tunes[j][i+pcOsc1B]=TUNER_OSC_INIT_OFFSET+j*TUNER_OSC_INIT_SCALE;
			tuner.tunes[j][i+pcFil1]=TUNER_FIL_INIT_OFFSET+j*TUNER_FIL_INIT_SCALE;
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
		synth_setCV(pcMVol,20000,0,0);
#else
		synth_setCV(pcMVol,20000,0,0);
#endif

		synth_setGate(pgASaw,1);
		synth_setGate(pgATri,0);
		synth_setGate(pgBSaw,1);
		synth_setGate(pgBTri,0);
		synth_setGate(pgPModFA,0);
		synth_setGate(pgPModFil,0);
		synth_setGate(pgSync,0);

		synth_setCV(pcResonance,0,0,0);
		synth_setCV(pcAPW,0,0,0);
		synth_setCV(pcBPW,0,0,0);
		synth_setCV(pcPModOscB,0,0,0);
		synth_setCV(pcExtFil,0,0,0);
		
		// init 8253
			// ch 0, mode 0, access 2 bytes, binary count
		i8253Write(0x3,0b00110000); 
			// ch 1, mode 0, access 2 bytes, binary count
		i8253Write(0x3,0b01110000); 
			// ch 2, mode 1, access 2 bytes, binary count
		i8253Write(0x3,0b10110010); 

		// tune oscs
			
			// init
		
		synth_setCV(pcResonance,0,0,0);
		for(i=0;i<P600_VOICE_COUNT;++i)
			synth_setCV(pcFil1+i,UINT16_MAX,0,0);
	
			// A oscs

		synth_setCV(pcVolA,UINT16_MAX,0,0);
		synth_setCV(pcVolB,0,0,0);

		for(i=0;i<P600_VOICE_COUNT;++i)
			tuneCV(pcOsc1A+i,pcAmp1+i);

			// B oscs

		synth_setCV(pcVolA,0,0,0);
		synth_setCV(pcVolB,UINT16_MAX,0,0);

		for(i=0;i<P600_VOICE_COUNT;++i)
			tuneCV(pcOsc1B+i,pcAmp1+i);

		// tune filters
			
			// init
		
		synth_setCV(pcVolA,0,0,0);
		synth_setCV(pcVolB,0,0,0);
		synth_setCV(pcResonance,UINT16_MAX,0,0);

		for(i=0;i<P600_VOICE_COUNT;++i)
			synth_setCV(pcFil1+i,0,0,0);
	
			// filters
		
		for(i=0;i<P600_VOICE_COUNT;++i)
			tuneCV(pcFil1+i,pcAmp1+i);

		// finish
		
		synth_setCV(pcResonance,0,0,0);
		for(i=0;i<P600_VOICE_COUNT;++i)
			synth_setCV(pcAmp1+i,0,0,0);
		
		synth_update();

		display_clear();
	}
}
