////////////////////////////////////////////////////////////////////////////////
// LFO (low frequency oscillator)
////////////////////////////////////////////////////////////////////////////////

#include "lfo.h"
#include <avr/pgmspace.h>

static const uint16_t sineShape[256] PROGMEM = // raised-cosine half wave, identical to the values the old init loop computed
{
	0,2,9,22,39,62,89,121,159,201,248,300,357,419,486,557,
	634,716,802,893,989,1090,1196,1306,1421,1541,1666,1796,1930,2069,2212,2360,
	2513,2670,2832,2999,3170,3345,3525,3710,3898,4092,4289,4491,4697,4908,5122,5341,
	5564,5791,6022,6258,6497,6740,6987,7238,7493,7752,8015,8281,8552,8825,9103,9384,
	9668,9956,10248,10543,10841,11143,11448,11756,12067,12382,12700,13020,13344,13670,14000,14332,
	14667,15005,15346,15689,16035,16383,16734,17087,17443,17801,18161,18524,18888,19255,19624,19995,
	20367,20742,21118,21497,21877,22258,22641,23026,23412,23800,24189,24579,24970,25363,25757,26152,
	26548,26945,27342,27741,28140,28540,28941,29342,29744,30146,30548,30951,31355,31758,32161,32565,
	32969,33373,33776,34179,34583,34986,35388,35790,36192,36593,36994,37394,37793,38192,38589,38986,
	39382,39777,40171,40564,40955,41345,41734,42122,42508,42893,43276,43657,44037,44416,44792,45167,
	45539,45910,46279,46646,47010,47373,47733,48091,48447,48800,49151,49499,49845,50188,50529,50867,
	51202,51534,51864,52190,52514,52834,53152,53467,53778,54086,54391,54693,54991,55286,55578,55866,
	56150,56431,56709,56982,57253,57519,57782,58041,58296,58547,58794,59037,59276,59512,59743,59970,
	60193,60412,60626,60837,61043,61245,61442,61636,61824,62009,62189,62364,62535,62702,62864,63021,
	63174,63322,63465,63604,63738,63868,63993,64113,64228,64338,64444,64545,64641,64732,64818,64900,
	64977,65048,65115,65177,65234,65286,65333,65375,65413,65445,65472,65495,65512,65525,65532,65535
};

// the 16 steps of the seq shape (a little random melody, see newSeq). The LFOs share it.
static uint16_t lfoSeq[16];

static void updateIncrement(struct lfo_s * lfo)
{
	lfo->increment=lfo->speed*(1-lfo->halfPeriod*2);
}

static void updateSpeed(struct lfo_s * lfo)
{
	int32_t spd;
	
	spd=exponentialCourse(UINT16_MAX-lfo->speedCV,8000.0f,65535.0f);

	lfo->speed=(spd<<4)>>lfo->slowShift; // slowShift is 0 except for the vibrato "low" range, so normally the speed is what it always was
}

static void handlePhaseOverflow(struct lfo_s * l)
{
	l->halfPeriod=1-l->halfPeriod;
	l->phase=l->halfPeriod?0x00ffffff:0;

	updateIncrement(l);

	switch(l->shape)
	{
	case lsPulse:
		l->rawOutput=l->halfPeriod*UINT16_MAX;
		break;
	case lsRand:
		l->rawOutput=random();
		break;
	default:
		;
	}
}

/*void LOWERCODESIZE lfo_setCVs(struct lfo_s * lfo, uint16_t spd, uint16_t lvl)
{
	lfo->levelCV=lvl;

	if(spd!=lfo->speedCV)
	{
		lfo->speedCV=spd;
		updateSpeed(lfo);
		updateIncrement(lfo);
	}
}*/

void LOWERCODESIZE lfo_setAmt(struct lfo_s * lfo, uint16_t lvl)
{
	lfo->levelCV=lvl;
}

void LOWERCODESIZE lfo_setFreq(struct lfo_s * lfo, uint16_t spd)
{
	if(spd!=lfo->speedCV)
	{
		lfo->speedCV=spd;
		updateSpeed(lfo);
		updateIncrement(lfo);
	}
}

// Makes up a new little melody for the seq shape: 16 steps with some repetition. The first step is random. Every other step is
// a new random value with a chance of 3/8; otherwise (5/8) it is the same as the step before with a chance of 5/8, or a copy of a random earlier step.
static void LOWERCODESIZE newSeq(void)
{
	uint8_t i,t;
	uint16_t r;

	for(i=0;i<16;++i)
	{
		r=random();

		if(i==0 || (r&0x0007)<3) // the first step, or 3 out of 8: a new value
		{
			lfoSeq[i]=random();
		}
		else if((r&0x0070)<0x0050) // bits 4..6 are 0..4 in 5 of 8 cases: the same as the step before
		{
			lfoSeq[i]=lfoSeq[i-1];
		}
		else // otherwise: a copy of any earlier step
		{
			t=(r>>8)%i;
			lfoSeq[i]=lfoSeq[t];
		}
	}
}

void LOWERCODESIZE lfo_setShape(struct lfo_s * lfo, lfoShape_t shape)
{
	// a new melody is only made when the seq shape is newly selected on this LFO (this function is also called regularly with the unchanged shape)
	if(shape==lsSeq && lfo->shape!=lsSeq)
		newSeq();

	lfo->shape=shape;
	
	if(lfo->noise==0)
		srandom(currentTick);
	
	lfo->noise=random();
}

// sets how much slower the LFO runs (shift 0 = normal speed, 3 = 8 times slower)
void LOWERCODESIZE lfo_setSlowShift(struct lfo_s * lfo, uint8_t shift)
{
	if(shift!=lfo->slowShift)
	{
		lfo->slowShift=shift;
		updateSpeed(lfo);
		updateIncrement(lfo);
	}
}

void LOWERCODESIZE lfo_resetPhase(struct lfo_s * lfo)
{
	lfo->phase = 0;
}


int16_t inline lfo_getOutput(struct lfo_s * lfo)
{
	return lfo->output;
}

const char * lfo_shapeName(lfoShape_t shape)
{
	switch(shape)
	{
	case lsPulse:
		return "pulse";
	case lsTri:
		return "tri";
	case lsRand:
		return "rand";
	case lsSine:
		return "sine";
	case lsNoise:
		return "noise";
	case lsSaw:
		return "saw";
	case lsStep8:
		return "step8";
	case lsRevSaw:
		return "revsaw";
	case lsSeq:
		return "seq";
	case lsStep4:
		return "step4";
	case lsBitTri:
		return "bit tri";
	case lsBitSine:
		return "bit sine";
	}
	
	return "";
}

void lfo_init(struct lfo_s * lfo)
{
	memset(lfo,0,sizeof(struct lfo_s));
}

inline void lfo_update(struct lfo_s * l)
{
	uint8_t incShift=0; // the staircase shapes run their phase slower, see below

	// if bit 24 or higher is set, it's an overflow -> a half period is done!
	
	if(l->phase>>24) 
		handlePhaseOverflow(l);
	
	// handle continuous shapes

	switch(l->shape)
	{
	case lsTri:
		l->rawOutput=l->phase>>8;
		break;
	case lsSine:
		l->rawOutput=computeShapeExact(l->phase,sineShape); // exact interpolation (computeShape drops the low byte of the step, which was up to 255 of 65535 off)
		break;
	case lsNoise:
		l->noise=lfsr(l->noise,(l->speedCV>>12)+1);
		l->rawOutput=l->noise;
		break;
	case lsSaw:
		l->rawOutput=l->phase>>9;
		if(l->halfPeriod)
			l->rawOutput=UINT16_MAX-l->rawOutput;
		break;
	case lsRevSaw: // the saw the other way round: it falls, then jumps up
		l->rawOutput=l->phase>>9;
		if(!l->halfPeriod)
			l->rawOutput=UINT16_MAX-l->rawOutput;
		break;
	case lsSeq: // the rising and falling phase (a triangle) picks one of the 16 steps
		l->rawOutput=l->phase>>9;
		if(l->halfPeriod)
			l->rawOutput=UINT16_MAX-l->rawOutput;
		l->rawOutput=lfoSeq[(l->rawOutput>>12)&0x0F];
		incShift=4; // the phase runs 16 times slower, so a step lasts about as long as a half period of the other shapes
		break;
	case lsStep8: // saw with only the top 3 bits: 8 levels
		l->rawOutput=l->phase>>9;
		if(l->halfPeriod)
			l->rawOutput=UINT16_MAX-l->rawOutput;
		l->rawOutput&=0xE000;
		// (no incShift: step8 runs at the same frequency as the saw. V2.34 ran it 8 times slower)
		break;
	case lsStep4: // saw with only the top 2 bits: 4 levels (the mask was 0x8000, which left only 2 levels, V2.34 has that too)
		l->rawOutput=l->phase>>9;
		if(l->halfPeriod)
			l->rawOutput=UINT16_MAX-l->rawOutput;
		l->rawOutput&=0xC000;
		// (no incShift: step4 runs at the same frequency as the saw. V2.34 ran it 4 times slower)
		break;
	case lsBitTri: // triangle with only the top 3 bits
		l->rawOutput=(l->phase>>8)&0xE000;
		break;
	case lsBitSine: // sine with only the top 4 bits: 16 levels, steppier than the 6 bits (64 levels) of before (and of V2.34)
		l->rawOutput=computeShapeExact(l->phase,sineShape)&0xF000;
		break;
	default:
		;
	}
	
	// phase increment (the slower shapes above advance by a fraction of it)
	
	if(incShift)
		l->phase+=(l->increment>>incShift);
	else
		l->phase+=l->increment;

	// compute output
	
	l->output=scaleU16S16(l->levelCV,(int32_t)l->rawOutput+INT16_MIN);
}


