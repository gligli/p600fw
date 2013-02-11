#ifndef ADSR_H
#define	ADSR_H

#include "p600.h"

typedef enum {sWait=0,sAttack=1,sDecay=2,sSustain=3,sRelease=4} adsr_stage_t;

struct adsr_s
{
	int8_t expOutput,gate,nextGate;
	
	adsr_stage_t stage;
	
	uint16_t currentLevel;
	
	uint8_t attackCV,decayCV,releaseCV;
	uint16_t sustainCV,levelCV;

	uint32_t phase,attackInc,decayInc,releaseInc;
	uint16_t output,final;
};

void adsr_setCVs(struct adsr_s * adsr, uint16_t atk, uint16_t dec, uint16_t sus, uint16_t rls, uint16_t lvl);
void adsr_setGate(struct adsr_s * adsr, int8_t gate);
void adsr_setShape(struct adsr_s * adsr, int8_t isExp);
uint16_t adsr_getOutput(struct adsr_s * adsr);

void adsr_init(struct adsr_s * adsr);
void adsr_update(struct adsr_s * a); // should be called at 19.5Khz

#endif	/* ADSR_H */

