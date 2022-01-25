////////////////////////////////////////////////////////////////////////////////
// LFO (low frequency oscillator)
////////////////////////////////////////////////////////////////////////////////

#include "lfo.h"

static uint16_t sineShape[256];

static void updateIncrement(struct lfo_s * lfo)
{
	lfo->increment=lfo->speed*(1-lfo->halfPeriod*2);
}

static void updateSpeed(struct lfo_s * lfo)
{
	int32_t spd;
	
	spd=exponentialCourse(UINT16_MAX-lfo->speedCV,8000.0f,65535.0f);

	lfo->speed=spd<<4;
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

void LOWERCODESIZE lfo_setShape(struct lfo_s * lfo, lfoShape_t shape)
{
	lfo->shape=shape;
	
	if(lfo->noise==0)
		srandom(currentTick);
	
	lfo->noise=random();
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
	}
	
	return "";
}

void lfo_init(struct lfo_s * lfo)
{
	int16_t i;
	
	memset(lfo,0,sizeof(struct lfo_s));
	
	for(i=0;i<256;++i)
		sineShape[i]=(cosf((i/255.0f+1.0f)*M_PI)+1.0f)/2.0f*65535.0f;
}

inline void lfo_update(struct lfo_s * l)
{
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
		l->rawOutput=computeShape(l->phase,sineShape,1);
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
	default:
		;
	}
	
	// phase increment
	
	l->phase+=l->increment;

	// compute output
	
	l->output=scaleU16S16(l->levelCV,(int32_t)l->rawOutput+INT16_MIN);
}


