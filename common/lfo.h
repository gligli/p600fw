#ifndef LFO_H
#define	LFO_H

#include "synth.h"

typedef enum
{
	lsPulse=0,lsTri=1,lsRand=2,lsSine=3,lsNoise=4,lsSaw=5,
	// the shapes below are new: step8 = 8 level staircase, revsaw = saw that falls (instead of rises), seq = 16 step random melody,
	// step4 = 2 level staircase (a pulse that follows the phase), bit tri / bit sine = triangle / sine with only 3 / 6 bits (coarse steps).
	// The numbers are stored in presets, they must not change.
	lsStep8=6,lsRevSaw=7,lsSeq=8,lsStep4=9,lsBitTri=10,lsBitSine=11
} lfoShape_t;

// number of LFO shapes (the shapes above are 0..LFO_SHAPE_COUNT-1)
#define LFO_SHAPE_COUNT 12

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

	// how many bits the speed is shifted right: 0 = normal, 3 = 8 times slower (the vibrato "low" range). Kept at the end of the struct so the other fields keep their place.
	uint8_t slowShift;
};

//void lfo_setCVs(struct lfo_s * lfo, uint16_t spd, uint16_t lvl);
void lfo_setAmt(struct lfo_s * lfo, uint16_t lvl);
void lfo_setFreq(struct lfo_s * lfo, uint16_t spd);
void lfo_setShape(struct lfo_s * lfo, lfoShape_t shape);
void lfo_setSlowShift(struct lfo_s * lfo, uint8_t shift);
void lfo_resetPhase(struct lfo_s * lfo);

int16_t lfo_getOutput(struct lfo_s * lfo);
const char * lfo_shapeName(lfoShape_t shape);

void lfo_init(struct lfo_s * lfo);
void lfo_update(struct lfo_s * lfo);

#endif	/* LFO_H */

