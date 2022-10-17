////////////////////////////////////////////////////////////////////////////////
// Tunes CVs using the 8253 timer, by measuring audio period
////////////////////////////////////////////////////////////////////////////////

#include <stdint.h>

#include "tuner.h"
#include "storage.h"
#include "sh.h"
#include "display.h"
#include "storage.h"
#include "scanner.h"

#define FF_P	0x01 // active low
#define CNTR_EN 0x02
#define FF_D	0x08
#define FF_CL	0x10 // active low

#define STATUS_TIMEOUT 1000000
#define STATUS_TIMEOUT_MAX_FAILURES 5

#define TUNER_TICK 2000000.0

#define TUNER_MIDDLE_C_HERTZ 261.63
#define TUNER_LOWEST_HERTZ (TUNER_MIDDLE_C_HERTZ/16)

#define TUNER_OSC_INIT_OFFSET 5000.0
#define TUNER_OSC_INIT_SCALE (65536.0/10.66)
#define TUNER_OSC_PRECISION -3 // higher is preciser but slower
#define TUNER_OSC_NTH_C_LO 3
#define TUNER_OSC_NTH_C_HI 6

#define TUNER_FIL_INIT_OFFSET 10000.0
#define TUNER_FIL_INIT_SCALE (65536.0/21.0)
#define TUNER_FIL_PRECISION -3 // higher is preciser but slower
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

	// full update once in a while
	sh_update();
}

static void i8253Write(uint8_t a,uint8_t v)
{
	io_write(a,v);
	CYCLE_WAIT(4);
}	

static uint8_t i8253Read(uint8_t a)
{
	CYCLE_WAIT(4);
	return io_read(a);
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

static NOINLINE void ffDoTimeout(void)
{
	++ff_timeoutCount;
#ifdef DEBUG
	print("bad flip flop status : ");
	phex(ff_step);
	phex(io_read(0x9));
	print(" timeout count : ");
	phex(ff_timeoutCount);
	print("\n");
#endif	
}


static void ffWaitStatus(uint8_t status)
{
	uint8_t s;
	uint32_t timeout=STATUS_TIMEOUT;

	do{
		s=io_read(0x9);
		--timeout;
	}while(((s>>1)&0x01)!=status && timeout);

	if (!timeout)
		ffDoTimeout();
}

static void ffWaitCounter(uint8_t status)
{
	uint8_t s;
	uint32_t timeout=STATUS_TIMEOUT;

	do{
		s=io_read(0x9);
		--timeout;
	}while(((s>>2)&0x01)!=status && timeout);

	if (!timeout)
		ffDoTimeout();
}

static NOINLINE uint16_t getPeriod(void)
{
	uint16_t c;
	
	// read counter, add to result
	c=i8253Read(0x1);
	c|=i8253Read(0x1)<<8;

		// ch1 load 0
	i8253Write(0x1,0x00);
	i8253Write(0x1,0x00);
	
		// ch2 load 1
	i8253Write(0x2,0x01);
	i8253Write(0x2,0x00);
	
	return UINT16_MAX-c;
}

static NOINLINE uint32_t measureAudioPeriod(uint8_t periods) // in 2Mhz ticks
{
	uint32_t res=0;
	
	// display / start maintainting CVs
	
	for(int8_t i=0;i<25;++i) // lower this and eg. filter tuning starts behaving badly
		whileTuning();
			
	// prepare flip flop
	
	ff_state=0;
	ff_step=0;
	ffMask(FF_P|FF_CL,FF_D|CNTR_EN);
	
	// prepare 8253
	
	getPeriod();
	
	// flip flop stuff (CF service manual section 2-16)
		
	while(periods)
	{
		// init

		ffMask(CNTR_EN,0);

		ffMask(FF_D,FF_P);
		ffWaitStatus(0);

		ffMask(FF_P,FF_CL);
		ffWaitStatus(1);

		// start
		
		ffMask(FF_CL,0);
		ffWaitCounter(0);

		ffMask(0,FF_CL);
		ffMask(FF_CL,0);
		ffWaitCounter(1);
		
		// reset

		ffMask(0,CNTR_EN|FF_D);
		
		// get result / display / ...
		
		--periods;

		res+=getPeriod();

		whileTuning();

		// detect untunable osc		
		
		if (ff_timeoutCount>=STATUS_TIMEOUT_MAX_FAILURES)
		{
			res=UINT32_MAX;
			break;
		}
	}
	
	return res;
}

static LOWERCODESIZE int8_t tuneOffset(p600CV_t cv,uint8_t nthC, uint8_t lowestNote, int8_t precision)
{
	int8_t i,relPrec;
	uint16_t estimate,bit;
	double p,tgtp;
	uint32_t ip;

	ff_timeoutCount=0;

	tgtp=TUNER_TICK/(TUNER_LOWEST_HERTZ*pow(2.0,nthC));
	
	estimate=UINT16_MAX;
	bit=0x8000;
	
	relPrec=precision+nthC;
	
	for(i=0;i<14;++i) // 14bit dac
	{
		if(estimate>tuner_computeCVFromNote(lowestNote,0,cv))
		{
			sh_setCV(cv,estimate,0);
			
			ip=measureAudioPeriod(1<<relPrec);
			if(ip==UINT32_MAX)
				return -1; // failure (untunable osc)
			
			p=(double)ip*pow(2.0,-relPrec);
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

void tuner_setNoteTuning(uint8_t note, double numSemitones)
{
	if (note >= TUNER_NOTE_COUNT) {
		return;
	}
	
	if (numSemitones < 0.0)
		numSemitones = 0.0;
	else if (numSemitones > 12.0)
		numSemitones = 12.0;
	
	currentPreset.perNoteTuning[note] = numSemitones * TUNING_UNITS_PER_SEMITONE;
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

	sh_setCV(ampCV,UINT16_MAX,0);
	
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

	sh_setCV(ampCV,0,0);
	sh_update();
}

static uint16_t extapolateUpperOctavesTunes(uint8_t oct, p600CV_t cv)
{
	uint32_t v;
	
	v=settings.tunes[TUNER_OCTAVE_COUNT-1][cv]-settings.tunes[TUNER_OCTAVE_COUNT-2][cv];
	
	v=settings.tunes[TUNER_OCTAVE_COUNT-1][cv]+(oct-TUNER_OCTAVE_COUNT+1)*v;
	
	return MIN(v,UINT16_MAX);
}

LOWERCODESIZE static void prepareSynth(void)
{
	display_clear();
	led_set(plTune,1,0);

#ifdef DEBUG
	sh_setCV(pcMVol,20000,0);
#else
	sh_setCV(pcMVol,0,0);
#endif
	// Update CV's and give final volume VCA CV filter time to close
	sh_update();
	MDELAY(150);

	sh_setGate(pgASaw,0);
	sh_setGate(pgATri,0);
	sh_setGate(pgBSaw,0);
	sh_setGate(pgBTri,0);
	sh_setGate(pgPModFA,0);
	sh_setGate(pgPModFil,0);
	sh_setGate(pgSync,0);

	sh_setCV(pcResonance,0,0);
	sh_setCV(pcAPW,0,0);
	sh_setCV(pcBPW,0,0);
	sh_setCV(pcPModOscB,0,0);
	sh_setCV(pcExtFil,0,0);

	// init 8253
		// ch 0, mode 0, access 2 bytes, binary count
	i8253Write(0x3,0b00110000); 
		// ch 1, mode 0, access 2 bytes, binary count
	i8253Write(0x3,0b01110000); 
		// ch 2, mode 1, access 2 bytes, binary count
	i8253Write(0x3,0b10110010); 

}

uint16_t tuner_computeCVPerOct(uint8_t note, p600CV_t cv)
{
	uint8_t loOct,hiOct;
	uint16_t loOctVal,hiOctVal;

	loOct=note/12;
	hiOct=loOct+1;

	if(loOct<TUNER_OCTAVE_COUNT)
		loOctVal=settings.tunes[loOct][cv];
	else
		loOctVal=extapolateUpperOctavesTunes(loOct,cv);

	if(hiOct<TUNER_OCTAVE_COUNT)
		hiOctVal=settings.tunes[hiOct][cv];
	else
		hiOctVal=extapolateUpperOctavesTunes(hiOct,cv);

    return (hiOctVal-loOctVal);
}

NOINLINE uint16_t tuner_computeCVFromNote(uint8_t note, uint8_t nextInterp, p600CV_t cv)
{
	uint8_t loOct,hiOct;
	uint16_t value,loOctVal,hiOctVal;
	uint32_t noteTuning; // in units of TUNING_UNITS_PER_SEMITONE
  
	loOct=note/12;
	hiOct=loOct+1;
	
	if(loOct<TUNER_OCTAVE_COUNT)
		loOctVal=settings.tunes[loOct][cv];
	else
		loOctVal=extapolateUpperOctavesTunes(loOct,cv);

	if(hiOct<TUNER_OCTAVE_COUNT)
		hiOctVal=settings.tunes[hiOct][cv];
	else
		hiOctVal=extapolateUpperOctavesTunes(hiOct,cv);
	
	noteTuning=currentPreset.perNoteTuning[note%12];
	noteTuning+=((uint32_t)nextInterp<<8)/12; // FIXME: avoid this divide?
	
	value=loOctVal;
	value+=(noteTuning*(hiOctVal-loOctVal))>>16;
	
	return value;
}

LOWERCODESIZE void tuner_init(void)
{
	int8_t i,j;
	
	memset(&tuner,0,sizeof(tuner));
	
	// theoretical base tuning
	
	for(j=0;j<TUNER_OCTAVE_COUNT;++j)
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
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
		// reinit tuner
		
		tuner_init();
		
		// prepare synth for tuning
		
		prepareSynth();
		
		// tune oscs
			
			// init
		
		sh_setCV(pcResonance,0,0);
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
		{
			sh_setCV(pcAmp1+i,0,0);
			sh_setCV(pcFil1+i,UINT16_MAX,0);
		}
	
			// A oscs

		sh_setGate(pgASaw,1);

		sh_setCV(pcVolA,UINT16_MAX,0);
		sh_setCV(pcVolB,0,0);

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			tuneCV(pcOsc1A+i,pcAmp1+i);

		sh_setGate(pgASaw,0);
		
			// B oscs

		sh_setGate(pgBSaw,1);

		sh_setCV(pcVolA,0,0);
		sh_setCV(pcVolB,UINT16_MAX,0);

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			tuneCV(pcOsc1B+i,pcAmp1+i);

		sh_setGate(pgBSaw,0);

		// tune filters
			
			// init
		
		sh_setCV(pcVolA,0,0);
		sh_setCV(pcVolB,0,0);
		sh_setCV(pcResonance,UINT16_MAX,0);

		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			sh_setCV(pcFil1+i,0,0);
	
			// filters
		
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			tuneCV(pcFil1+i,pcAmp1+i);

		// finish
		
		sh_setCV(pcResonance,0,0);
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			sh_setCV(pcAmp1+i,0,0);
		
		sh_update();

		display_clear();
		
		settings_save();
	}
}

LOWERCODESIZE void tuner_scalingAdjustment(void)
{
	p600CV_t cv=0;
	int32_t lo,hi,delta;
	int8_t i;
	uint8_t ps=0;
	
	prepareSynth();
	
	for(;;)
	{
		BLOCK_INT
		{
			io_write(0x08,0);

			CYCLE_WAIT(10);

			ps=io_read(0x0a);
		}

		if(ps&2) //pb1
		{
			cv=(cv+1)%18;
		}
		else if(ps&4) //pb2
		{
			cv=(cv+18-1)%18;
		}
		
		tuner.currentCV=cv;
		
		sh_setCV(pcResonance,cv>=pcFil1?UINT16_MAX:0,0);
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
		{
			sh_setCV(pcAmp1+i,cv%SYNTH_VOICE_COUNT==i?UINT16_MAX:0,0);
			sh_setCV(pcFil1+i,UINT16_MAX,0);
			sh_setCV(pcOsc1A+i,UINT16_MAX,0);
			sh_setCV(pcOsc1B+i,UINT16_MAX,0);
		}

		sh_setCV(pcVolA,cv<pcOsc1B?UINT16_MAX:0,0);
		sh_setGate(pgASaw,cv<pcOsc1B?UINT16_MAX:0);

		sh_setCV(pcVolB,cv>=pcOsc1B&&cv<pcFil1?UINT16_MAX:0,0);
		sh_setGate(pgBSaw,cv>=pcOsc1B&&cv<pcFil1?UINT16_MAX:0);

		if(cv<pcFil1) // oscillator frequencies, first 6 OSCA and 6 OSCB
		{
			sh_setCV(cv,TUNER_OSC_INIT_OFFSET+3*TUNER_OSC_INIT_SCALE,0);
			lo=measureAudioPeriod(8);
			sh_setCV(cv,TUNER_OSC_INIT_OFFSET+7*TUNER_OSC_INIT_SCALE,0);
			hi=measureAudioPeriod(128);
		}
		else
		{
			sh_setCV(cv,TUNER_FIL_INIT_OFFSET+5*TUNER_FIL_INIT_SCALE,0);
			lo=measureAudioPeriod(8);
			sh_setCV(cv,TUNER_FIL_INIT_OFFSET+9*TUNER_FIL_INIT_SCALE,0);
			hi=measureAudioPeriod(128);
		}
		
		for(i=0;i<SYNTH_VOICE_COUNT;++i)
			sh_setCV(pcAmp1+i,0,0);

		delta=(hi-lo)>>8;
		
#ifdef DEBUG
		phex16(hi>>16);
		phex16(hi);
		print("\n");
		phex16(lo>>16);
		phex16(lo);
		print("\n");
		phex16(delta>>16);
		phex16(delta);
		print("\n");
#endif
		
		delta=MIN(delta,99);
		delta=MAX(delta,-99);
		
		sevenSeg_setNumber(abs(delta));
		led_set(plDot,delta<0,0);

		for(i=0;i<50;++i)
		{
			MDELAY(10);
			display_update(1);
			sh_update();
		}
	}
}
