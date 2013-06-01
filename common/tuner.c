////////////////////////////////////////////////////////////////////////////////
// Tunes a CV using the 8253 timer, by counting audio cycles
////////////////////////////////////////////////////////////////////////////////

#include "tuner.h"
#include "storage.h"
#include "synth.h"
#include "display.h"
#include "storage.h"

#define FF_P	0x01 // active low
#define CNTR_EN 0x02
#define FF_D	0x08
#define FF_CL	0x10 // active low

#define STATUS_TIMEOUT UINT16_MAX
#define STATUS_TIMEOUT_MAX_FAILURES 5

#define TUNER_TICK 2000000.0

#define TUNER_MIDDLE_C_HERTZ 261.63
#define TUNER_LOWEST_HERTZ (TUNER_MIDDLE_C_HERTZ/16)

#define TUNER_VCA_LEVEL UINT16_MAX

#define TUNER_OSC_INIT_OFFSET 5000.0
#define TUNER_OSC_INIT_SCALE (65536.0/11.0)
#define TUNER_OSC_PRECISION -3 // higher is preciser but slower
#define TUNER_OSC_NTH_C_LO 3
#define TUNER_OSC_NTH_C_HI 6

#define TUNER_FIL_INIT_OFFSET 10000.0
#define TUNER_FIL_INIT_SCALE (65536.0/22.0)
#define TUNER_FIL_PRECISION -2 // higher is preciser but slower
#define TUNER_FIL_NTH_C_LO 4
#define TUNER_FIL_NTH_C_HI 7

static struct
{
	p600CV_t currentCV;
} tuner;

static LOWERCODESIZE void whileTuning(void)
{
	// display current osc
	if(tuner.currentCV<pcOsc1B)
		sevenSeg_setAscii('a','1'+tuner.currentCV-pcOsc1A);
	else if(tuner.currentCV<pcFil1)
		sevenSeg_setAscii('b','1'+tuner.currentCV-pcOsc1B);
	else
		sevenSeg_setAscii('f','1'+tuner.currentCV-pcFil1);

	display_update(1);

	synth_update();
}

static uint8_t ff_state=0;
static uint8_t ff_step=0;
static uint8_t ff_timeoutCount=0;
	
static NOINLINE void ffMask(uint8_t set,uint8_t clear)
{
	ff_state|=set;
	ff_state&=~clear;
	
	io_write(0x0e,ff_state);
	CYCLE_WAIT(4);
	
	++ff_step;
}

static FORCEINLINE void counter_reset(void)
{
	TCNT1=0;
}

static FORCEINLINE void counter_start(void)
{
	TCCR1B=1<<CS11;
}

static FORCEINLINE void counter_stop(void)
{
	TCCR1B=0;
}

static FORCEINLINE uint16_t counter_getValue(void)
{
	return TCNT1;
}

static void ffWaitStatus(void)
{
	uint8_t s;
	uint16_t timeout=STATUS_TIMEOUT;
	
	do
	{
		s=io_read(0x9);
		--timeout;
	}
	while((s&2)==0 && timeout);

	if (!timeout)
	{
		++ff_timeoutCount;
#ifdef DEBUG
		print("bad flip flop status : ");
		phex(ff_step);
		phex(s);
		print(" timeout count : ");
		phex(ff_timeoutCount);
		print("\n");
#endif	
	}
}

static NOINLINE uint32_t measureAudioPeriod(uint8_t periods) // in ticks
{
	uint32_t res=0;
	
	// prepare flip flop

	ff_state=0;
	ff_step=0;
	ffMask(FF_P|FF_CL,FF_D|CNTR_EN);

	// prepare timer

	counter_stop();

	while(periods--)
	{
		// prepare

		counter_reset();

		whileTuning();
		synth_maintainCV(tuner.currentCV,0);
	
		// wait for peak

		ffMask(0,FF_P);
		ffMask(FF_P,0);
		ffWaitStatus();

		// wait for peak

		ffMask(0,FF_P);
		ffMask(FF_P,0);
		ffWaitStatus();
		counter_start();

		// wait for peak

		ffMask(0,FF_P);
		ffMask(FF_P,0);
		ffWaitStatus();
		counter_stop();
		
		res+=(uint32_t)counter_getValue();

		synth_maintainCV(tuner.currentCV,1);
	}
	
	return res;	
}


static LOWERCODESIZE int8_t tuneOffset(p600CV_t cv,uint8_t nthC, uint8_t lowestNote, int8_t precision)
{
	int8_t i,relPrec;
	uint16_t estimate,bit;
	float p,tgtp;
	uint32_t ip;

	ff_timeoutCount=0;

	tgtp=TUNER_TICK/(TUNER_LOWEST_HERTZ*powf(2.0f,nthC));
	
	estimate=UINT16_MAX;
	bit=0x8000;
	
	relPrec=precision+nthC;
	
	for(i=0;i<=14;++i) // 14bit dac
	{
		if(estimate>tuner_computeCVFromNote(lowestNote,0,cv))
		{
			synth_setCV(cv,estimate,0);
			
			ip=measureAudioPeriod(1<<relPrec);
			if(ip==UINT32_MAX)
				return -1; // failure (untunable osc)
			
			p=(double)ip*powf(2.0f,-relPrec);
		}
		else
		{
			p=FLT_MAX;
		}
		
		// adjust estimate
		if (p>tgtp)
			estimate+=bit;
		else
			estimate-=bit;

		// on to finer changes
		bit>>=1;
		
	}

	settings.tunes[nthC][cv]=estimate;

#ifdef DEBUG		
	print("cv ");
	phex16(estimate);
	print(" per ");
	phex16(p);
	print(" ");
	phex16(tgtp);
	print("\n");
#endif
	
	return 0;
}

static LOWERCODESIZE void tuneCV(p600CV_t oscCV, p600CV_t ampCV)
{
#ifdef DEBUG		
	print("\ntuning ");phex(oscCV);print("\n");
#endif
	int8_t isOsc,i;
	
	// init
	
	tuner.currentCV=oscCV;
	isOsc=(oscCV<pcFil1);

	// open VCA

	synth_setCV(ampCV,TUNER_VCA_LEVEL,0);
	
	// update all params
	
	synth_update();

	// tune

	if (isOsc)
	{
		for(i=TUNER_OSC_NTH_C_LO;i<=TUNER_OSC_NTH_C_HI;++i)
			if (tuneOffset(oscCV,i,12*(TUNER_OSC_NTH_C_LO-2),TUNER_OSC_PRECISION))
				break;

		// extrapolate for octaves that aren't directly tunable
		
		for(i=TUNER_OSC_NTH_C_LO-1;i>=0;--i)
			settings.tunes[i][oscCV]=(uint32_t)2*settings.tunes[i+1][oscCV]-settings.tunes[i+2][oscCV];

		for(i=TUNER_OSC_NTH_C_HI+1;i<TUNER_OCTAVE_COUNT;++i)
			settings.tunes[i][oscCV]=(uint32_t)2*settings.tunes[i-1][oscCV]-settings.tunes[i-2][oscCV];
	}
	else
	{
		for(i=TUNER_FIL_NTH_C_LO;i<=TUNER_FIL_NTH_C_HI;++i)
			if (tuneOffset(oscCV,i,12*(TUNER_FIL_NTH_C_LO-1),TUNER_FIL_PRECISION))
				break;

		for(i=TUNER_FIL_NTH_C_LO-1;i>=0;--i)
			settings.tunes[i][oscCV]=(uint32_t)2*settings.tunes[i+1][oscCV]-settings.tunes[i+2][oscCV];

		for(i=TUNER_FIL_NTH_C_HI+1;i<TUNER_OCTAVE_COUNT;++i)
			settings.tunes[i][oscCV]=(uint32_t)2*settings.tunes[i-1][oscCV]-settings.tunes[i-2][oscCV];
	}
	
	// close VCA

	synth_setCV(ampCV,0,0);
	synth_update();
}

static uint16_t extapolateUpperOctavesTunes(uint8_t oct, p600CV_t cv)
{
	uint32_t v;
	
	v=settings.tunes[TUNER_OCTAVE_COUNT-1][cv]-settings.tunes[TUNER_OCTAVE_COUNT-2][cv];
	
	v=settings.tunes[TUNER_OCTAVE_COUNT-1][cv]+(oct-TUNER_OCTAVE_COUNT+1)*v;
	
	return MIN(v,UINT16_MAX);
}

NOINLINE uint16_t tuner_computeCVFromNote(uint8_t note, uint8_t nextInterp, p600CV_t cv)
{
	uint8_t loOct,hiOct;
	uint16_t value,loVal,hiVal;
	uint32_t semiTone;
	
	loOct=note/12;
	hiOct=loOct+1;
	
	if(loOct<TUNER_OCTAVE_COUNT)
		loVal=settings.tunes[loOct][cv];
	else
		loVal=extapolateUpperOctavesTunes(loOct,cv);

	if(hiOct<TUNER_OCTAVE_COUNT)
		hiVal=settings.tunes[hiOct][cv];
	else
		hiVal=extapolateUpperOctavesTunes(hiOct,cv);
	
	semiTone=(((uint32_t)(note%12)<<16)+((uint16_t)nextInterp<<8))/12;
	
	value=loVal;
	value+=(semiTone*(hiVal-loVal))>>16;
	
	return value;
}

LOWERCODESIZE void tuner_init(void)
{
	int8_t i,j;
	
	memset(&tuner,0,sizeof(tuner));
	
	for(j=0;j<TUNER_OCTAVE_COUNT;++j)
		for(i=0;i<P600_VOICE_COUNT;++i)
		{
			settings.tunes[j][i+pcOsc1A]=TUNER_OSC_INIT_OFFSET+j*TUNER_OSC_INIT_SCALE;
			settings.tunes[j][i+pcOsc1B]=TUNER_OSC_INIT_OFFSET+j*TUNER_OSC_INIT_SCALE;
			settings.tunes[j][i+pcFil1]=TUNER_FIL_INIT_OFFSET+j*TUNER_FIL_INIT_SCALE;
		}
}

LOWERCODESIZE void tuner_tuneSynth(void)
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
		synth_setCV(pcMVol,20000,0);
#endif

		synth_setGate(pgASaw,1);
		synth_setGate(pgATri,0);
		synth_setGate(pgBSaw,1);
		synth_setGate(pgBTri,0);
		synth_setGate(pgPModFA,0);
		synth_setGate(pgPModFil,0);
		synth_setGate(pgSync,0);

		synth_setCV(pcResonance,0,0);
		synth_setCV(pcAPW,UINT16_MAX,0);
		synth_setCV(pcBPW,UINT16_MAX,0);
		synth_setCV(pcPModOscB,0,0);
		synth_setCV(pcExtFil,0,0);
		
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
		
		synth_setGate(pgASaw,0);
		synth_setGate(pgBSaw,0);
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
		
		settings_save();
	}
}
