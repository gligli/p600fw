#ifndef ADSR_H
#define	ADSR_H

#include "synth.h"

typedef enum
{
	sWait=0,sAttack=1,sDecay=2,sSustain=3,sRelease=4,sDone=5
} adsrStage_t;

struct adsr_s
{
	uint32_t stageIncrement;	
	uint32_t phase;
	uint32_t attackIncrement,decayIncrement,releaseIncrement; 
	
	uint16_t sustainCV,levelCV;
	uint16_t attackCV,decayCV,releaseCV;
	uint16_t stageLevel,stageAdd,stageMul;
	uint16_t output;

	int8_t shape,gate;
	uint8_t speedShift;
	
	adsrStage_t stage;
};

void adsr_setCVs(struct adsr_s * adsr, uint16_t atk, uint16_t dec, uint16_t sus, uint16_t rls, uint16_t lvl, uint8_t mask);
void adsr_setGate(struct adsr_s * adsr, int8_t gate);

void adsr_setShape(struct adsr_s * adsr, int8_t shape);
void adsr_setSpeedShift(struct adsr_s * adsr, uint8_t shift);

adsrStage_t adsr_getStage(struct adsr_s * adsr);
uint16_t adsr_getOutput(struct adsr_s * adsr);

void adsr_reset(struct adsr_s * adsr);

void adsr_init(struct adsr_s * adsr);
void adsr_update(struct adsr_s * adsr);

#endif	/* ADSR_H */

