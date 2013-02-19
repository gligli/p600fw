#ifndef LFO_H
#define	LFO_H

#include "p600.h"

typedef enum
{
	lsOff=0,lsPulse=1,lsTri=2,lsRand=3,lsSine=4
} lfoShape_t;

struct lfo_s
{
	uint32_t phase;
	int32_t increment;	
	
	uint16_t levelCV,speedCV;
	uint16_t rawOutput;
	int16_t output;
	
	uint8_t speedShift;
	int8_t halfPeriod;
	
	lfoShape_t shape;
};

void lfo_setCVs(struct lfo_s * lfo, uint16_t spd, uint16_t lvl);
void lfo_setShape(struct lfo_s * lfo, lfoShape_t shape);
void lfo_setSpeedShift(struct lfo_s * lfo, uint8_t shift);

int16_t lfo_getOutput(struct lfo_s * lfo);
const char * lfo_shapeName(lfoShape_t shape);

void lfo_init(struct lfo_s * lfo, unsigned int randSeed);
void lfo_update(struct lfo_s * lfo);

#endif	/* LFO_H */

