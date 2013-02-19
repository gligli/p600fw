////////////////////////////////////////////////////////////////////////////////
// ADSR envelope, ported from electricdruid's ENVGEN7_MOOG.ASM
////////////////////////////////////////////////////////////////////////////////

/*
;  Programme fait par tom wiltshire http://www.electricdruid.com
;  Pour module VCADSR, Programme original nommé ENVGEN7.ASM (premiere version).
;  La 2ieme ver ENVGEN7B.ASM ne marchait pas.
;  Les lignes 884 à 893 ont été ajoutées pour annuler l'effet
;  du pot nommé TIME CV (RC2/AN6, PIN8) qui changait toute la durée
;  de l'envelope ADSR (étirait ou écrasait.. selon voltage 0-5v
;  présent à la patte PIN8).
;  Les tables nommées PhaseLookupHi, PhaseLookupMid, PhaseLookupLo (lignes 1002 à 1074 ) 
;  ont été modifiées pour suivre le graticule Moog des pots Attack, Decay, Release.
;  de 2msec. à 10sec.
;  Juin 2008. JPD.
;  ---------------------------------------------------------------------
;
;  This program provides a versatile envelope generator on a single chip.
;  It is designed as a modern version of the CEM3312 or SSM2056 ICs.
;  Analogue output is provided as a PWM output, which requires LP
;  filtering to be useable.
;
;  Hardware Notes:
;   PIC16F684 running at 20 MHz using external crystal
; Six analogue inputs:
;   RA1/AN1: 0-5V Attack Time CV
;   RA2/AN2: 0-5V Decay Time CV
;   RC0/AN4: 0-5V Sustain Level CV
;   RC1/AN5: 0-5V Release Time CV
;   RC2/AN6: 0-5V Time Adjust CV (Keyboard CV or velocity, for example)
;   RC3/AN7: 0-5V Output Level CV
; Two digital inputs:
;   RA3: Gate Input
;   RC4: Exp/Lin Input (High is Linear)
; One digital output
;   RA0: Gate LED output
;
;  This version started as (ENVGEN4LIN.ASM), a test version without any of
;  the complications of the exponential output. Instead it passes
;  the linear PHASE value directly to the PWM output.
;  This should allow me to test the rest of the code, before trying to
;  add the complex (for a PIC) interpolation and lookp maths required for the 
;  exponential curve output 
;
;  29th Aug 06 - got this basically working.
;  2nd Sept 06 - ENVGEN5.ASM - Added exponential output.
; Still have 278 bytes left!
*/ 

#include "adsr.h"
#include "adsr_lookups.h"

#define ADSR_SPEED_SHIFT 1

static uint32_t getPhaseInc(uint8_t v)
{
	uint32_t r=0;
	
	r|=(uint32_t)phaseLookupLo[v];
	r|=(uint32_t)phaseLookupMid[v]<<8;
	r|=(uint32_t)phaseLookupHi[v]<<16;
	
	return r;
}

static inline uint16_t computeOutput(uint32_t phase, uint16_t lookup[], int8_t isExp)
{
	if(isExp)
		return computeShape(phase,lookup);
	else
		return phase>>8; // 20bit -> 16 bit
}

static inline void updateStageVars(struct adsr_s * a, adsrStage_t s)
{
	switch(s)
	{
	case sAttack:
		a->stageAdd=((uint32_t)a->stageLevel*a->levelCV)>>16;
		a->stageMul=((uint32_t)(UINT16_MAX-a->stageLevel)*a->levelCV)>>16;
		a->stageIncrement=a->attackIncrement;
		break;
	case sDecay:
		a->stageAdd=((uint32_t)a->sustainCV*a->levelCV)>>16;
		a->stageMul=((uint32_t)(UINT16_MAX-a->sustainCV)*a->levelCV)>>16;
		a->stageIncrement=a->decayIncrement;
		break;
	case sSustain:
		a->stageAdd=0;
		a->stageMul=a->levelCV;
		break;
	case sRelease:
		a->stageAdd=0;
		a->stageMul=((uint32_t)a->stageLevel*a->levelCV)>>16;
		a->stageIncrement=a->releaseIncrement;
		break;
	default:
		;
	}
}

void adsr_setCVs(struct adsr_s * adsr, uint16_t atk, uint16_t dec, uint16_t sus, uint16_t rls, uint16_t lvl)
{
	adsr->sustainCV=sus;
	adsr->levelCV=lvl;
	
	adsr->attackIncrement=getPhaseInc(atk>>8)>>ADSR_SPEED_SHIFT<<4; // phase is 20 bits, from bit 4 to bit 23
	adsr->decayIncrement=getPhaseInc(dec>>8)>>ADSR_SPEED_SHIFT<<4;
	adsr->releaseIncrement=getPhaseInc(rls>>8)>>ADSR_SPEED_SHIFT<<4;
	
	// immediate update of env settings
	
	updateStageVars(adsr,adsr->stage);
}

void inline adsr_setGate(struct adsr_s * adsr, int8_t gate)
{
	if(adsr->gate!=gate)
	{
		adsr->nextGate=gate;
		adsr->gateChanged=1;
	}
}

void inline adsr_setShape(struct adsr_s * adsr, int8_t isExp)
{
	adsr->expOutput=isExp;
}

adsrStage_t inline adsr_getStage(struct adsr_s * adsr)
{
	return adsr->stage;
}

uint16_t inline adsr_getOutput(struct adsr_s * adsr)
{
	return adsr->output;
}

void adsr_init(struct adsr_s * adsr)
{
	memset(adsr,0,sizeof(struct adsr_s));
}

void inline adsr_update(struct adsr_s * a)
{
	// handle gate
	
	if(a->gateChanged)
	{
		a->phase=0;
		a->stageLevel=((uint32_t)a->output<<16)/a->levelCV;
		
		if(a->nextGate)
		{
			a->stage=sAttack;
			updateStageVars(a,sAttack);
		}
		else
		{
			a->stage=sRelease;
			updateStageVars(a,sRelease);
		}
		
		a->gate=a->nextGate;
		a->gateChanged=0;
	}
	
	// shortcut for inactive envelopes

	if (a->stage==sWait)
	{
		a->output=0;
		return;
	}

	// handle phase overflow
	
	if(a->phase>>24) // if bit 24 or higher is set, it's an overflow -> a timed stage is done!
	{
		a->phase=0;
		a->stageIncrement=0;

		++a->stage;

		switch(a->stage)
		{
		case sDecay:
			a->output=a->levelCV;
			updateStageVars(a,sDecay);
			return;
		case sSustain:
			updateStageVars(a,sSustain);
			return;			
		case sDone:
			a->stage=sWait;
			a->output=0;
			return;
		default:
			;
		}
	}

	// compute output level
	
	uint32_t o=0;
	
	switch(a->stage)
	{
	case sAttack:
		o=computeOutput(a->phase,attackCurveLookup,a->expOutput);
		break;
	case sDecay:
	case sRelease:
		o=UINT16_MAX-computeOutput(a->phase,decayCurveLookup,a->expOutput);
		break;
	case sSustain:
		o=a->sustainCV;
		break;
	default:
		;
	}
	
	a->output=((o*a->stageMul)>>16)+a->stageAdd;

	// phase increment
	
	a->phase+=a->stageIncrement;
}

