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

static inline uint32_t getPhaseInc(uint8_t v)
{
	uint32_t r=0;
	
	r|=phaseLookupLo[v];
	r|=phaseLookupMid[v]<<8;
	r|=phaseLookupHi[v]<<16;
	
	return r;
}

static inline int8_t incrementPhase(uint32_t * phase, uint32_t inc) // return true if overflowed
{
	*phase+=inc;
	return *phase>=1<<20;
}

static inline uint16_t computeOutput(uint32_t phase, uint16_t scale, uint16_t add, uint8_t lookup[], int8_t isExp, int8_t complement)
{
	uint16_t r;

	if(isExp)
	{
		uint8_t a,b;
		uint16_t x;
		
		x=phase&0x0fff;
		b=a=phase>>12;
		
		if(a<UINT8_MAX)
			b=a+1;
		
		a=lookup[a];
		b=lookup[b];
		
		phase=((uint32_t)a<<12)+(uint32_t)x*(b-a);
	}
	
	r=(phase*(scale>>4))>>16;
	
	if (complement)
		r=UINT16_MAX-r;

	return r+add;
}

void adsr_setCVs(struct adsr_s * adsr, uint16_t atk, uint16_t dec, uint16_t sus, uint16_t rls, uint16_t lvl)
{
	adsr->sustainCV=sus;
	adsr->levelCV=lvl;
	
	adsr->attackCV=atk>>8;
	adsr->decayCV=dec>>8;
	adsr->releaseCV=rls>>8;

	adsr->attackInc=getPhaseInc(adsr->attackCV);
	adsr->decayInc=getPhaseInc(adsr->decayCV);
	adsr->releaseInc=getPhaseInc(adsr->releaseCV);
}

void adsr_setGate(struct adsr_s * adsr, int8_t gate)
{
	adsr->nextGate=gate;
}

void adsr_setShape(struct adsr_s * adsr, int8_t isExp)
{
	adsr->expOutput=isExp;
}

uint16_t adsr_getOutput(struct adsr_s * adsr)
{
	return adsr->final;
}

void adsr_init(struct adsr_s * adsr)
{
	memset(adsr,0,sizeof(struct adsr_s));
}

void adsr_update(struct adsr_s * a)
{
	// handle gate
	
	if(a->gate!=a->nextGate)
	{
		a->phase=0;
		a->currentLevel=a->output;
		
		if(a->nextGate)
		{
			a->stage=sAttack;
		}
		else
		{
			a->stage=sRelease;
		}
		
		a->gate=a->nextGate;
	}
	
	// shortcut for inactive envelopes

	if (a->stage==sWait)
	{
		a->final=0;
		return;
	}

	// handle phase increment
	
	int8_t overflow=0;
	
	switch(a->stage)
	{
	case sAttack:
		overflow=incrementPhase(&a->phase,a->attackInc);
		break;
	case sDecay:
		overflow=incrementPhase(&a->phase,a->decayInc);
		break;
	case sRelease:
		overflow=incrementPhase(&a->phase,a->releaseInc);
		break;
	}
	
	// is a timed stage done?
	
	if(overflow)
	{
		phex(a->stage);
		a->phase=0;

		++a->stage;

		if(a->stage>sRelease)
		{
			a->stage=sWait;
			a->final=0;
			return;
		}
	}
	
	// compute output level
	
	uint16_t o=0;
	
	switch(a->stage)
	{
	case sAttack:
		o=computeOutput(a->phase,UINT16_MAX-a->currentLevel,a->currentLevel,attackCurveLookup,a->expOutput,0);
		break;
	case sDecay:
		o=computeOutput(a->phase,UINT16_MAX-a->sustainCV,0,decayCurveLookup,a->expOutput,1);
		break;
	case sSustain:
		o=a->sustainCV;
		break;
	case sRelease:
		o=computeOutput(a->phase,a->currentLevel,a->currentLevel,decayCurveLookup,a->expOutput,1);
		break;
	}
	
	a->final=(a->output*a->levelCV)>>16;
	a->output=o;
}

