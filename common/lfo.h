#ifndef LFO_H
#define	LFO_H

#include "synth.h"

typedef enum
{
	lsPulse=0,lsTri=1,lsRand=2,lsSine=3,lsNoise=4,lsSaw=5
} lfoShape_t;

struct lfo_s
{
	uint32_t noise;
	
	uint32_t phase;
	int32_t speed;
	int32_t increment;	
	
	uint16_t levelCV,speedCV;
	uint16_t rawOutput;
	int16_t output;
	
	int8_t halfPeriod;
	
	lfoShape_t shape;
};

//void lfo_setCVs(struct lfo_s * lfo, uint16_t spd, uint16_t lvl);
void lfo_setAmt(struct lfo_s * lfo, uint16_t lvl);
void lfo_setFreq(struct lfo_s * lfo, uint16_t spd);
void lfo_setShape(struct lfo_s * lfo, lfoShape_t shape);
void lfo_resetPhase(struct lfo_s * lfo);

int16_t lfo_getOutput(struct lfo_s * lfo);
const char * lfo_shapeName(lfoShape_t shape);

void lfo_init(struct lfo_s * lfo);
void lfo_update(struct lfo_s * lfo);

#endif	/* LFO_H */

