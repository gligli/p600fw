#ifndef ADSR_H
#define	ADSR_H

#include "p600.h"

typedef enum {sWait=0,sAttack=1,sDecay=2,sSustain=3,sRelease=4,sDone=5} adsr_stage_t;

struct adsr_s
{
	adsr_stage_t stage;
	int8_t expOutput,gate,nextGate;
	uint8_t attackCV,decayCV,releaseCV;
	uint8_t currentLevel;
	
	uint16_t sustainCV,levelCV;
	uint16_t output,final;

	uint32_t currentIncrement;	
	uint32_t phase;
	uint32_t attackIncrement,decayIncrement,releaseIncrement; 
};

void adsr_setCVs(struct adsr_s * adsr, uint16_t atk, uint16_t dec, uint16_t sus, uint16_t rls, uint16_t lvl);
void adsr_setGate(struct adsr_s * adsr, int8_t gate);
void adsr_setShape(struct adsr_s * adsr, int8_t isExp);
uint16_t adsr_getOutput(struct adsr_s * adsr);

void adsr_init(struct adsr_s * adsr);
void adsr_update(struct adsr_s * a);

#endif	/* ADSR_H */

