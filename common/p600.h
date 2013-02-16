#ifndef P600_H
#define	P600_H

#include "utils.h"
#include "print.h"
#include "hardware.h"

#define DEBUG

#define P600_VOICE_COUNT 6

////////////////////////////////////////////////////////////////////////////////
// Prophet 600 definitions
////////////////////////////////////////////////////////////////////////////////

typedef enum
{
	plSeq1=0,plSeq2=1,plArpUD=2,plArpAssign=3,plPreset=4,plRecord=5,plToTape=6,plFromTape=7,plTune=8,plDot=9
} p600LED_t;

typedef enum
{
	ppMixer=0,ppCutoff=1,ppResonance=2,ppFilEnvAmt=3,ppFilRel=4,ppFilSus=5,
	ppFilDec=6,ppFilAtt=7,ppAmpRel=8,ppAmpSus=9,ppAmpDec=10,ppAmpAtt=11,
	ppGlide=12,ppBPW=13,ppMVol=14,ppMTune=15,ppPitchWheel=16,ppModWheel=22,
	ppSpeed,ppAPW,ppPModFilEnv,ppLFOFreq,ppPModOscB,ppLFOAmt,ppFreqB,ppFreqA,ppFreqBFine
} p600Pot_t;

typedef enum
{
	pcOsc1A=0,pcOsc2A,pcOsc3A,pcOsc4A,pcOsc5A,pcOsc6A,           
	pcOsc1B=6,pcOsc2B,pcOsc3B,pcOsc4B,pcOsc5B,pcOsc6B,           
	pcFil1=12,pcFil2,pcFil3,pcFil4,pcFil5,pcFil6,                
	pcAmp1=18,pcAmp2,pcAmp3,pcAmp4,pcAmp5,pcAmp6,                
	pcPModOscB=24,pcVolA,pcVolB,pcMVol,pcAPW,pcExtFil,pcRes,pcBPW
} p600CV_t;

typedef enum
{
	pgASaw=0,pgATri,pgSync,pgBSaw,pgBTri,pgPModFA,pgPModFil
} p600Gate_t;

typedef enum
{
	pb0=0,pb1,pb2,pb3,pb4,pb5,pb6,pb7,                                            
	pb8=8,pb9,pbArpUD,pbArpAssign,pbPreset,pbRecord,pbToTape,pbFromTape,          
	pbSeq1=16,pbSeq2,pbTune,                                                      
	pbASqr=24,pbBSqr,pbFilFull,pbFilHalf,pbLFOShape,pbLFOFreq,pbLFOPW,pbLFOFil,   
	pbASaw=32,pbATri,pbSync,pbBSaw,pbBTri,pbPModFA,pbPModFil,pbUnisson
} p600Button_t;

void p600_buttonEvent(p600Button_t button, int pressed);
void p600_keyEvent(uint8_t key, int pressed);

void p600_init(void);
void p600_update(void);
void p600_slowInterrupt(void);
void p600_fastInterrupt(void);

#endif	/* P600_H */

