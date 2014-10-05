#ifndef SYNTH_H
#define	SYNTH_H

#include "utils.h"
#include "print.h"
#include "hardware.h"

//#define DEBUG
//#define RELEASE "Ver 2.00"

#define UART_USE_HW_INTERRUPT // this needs an additional wire that goes from pin C4 to pin E4

#ifndef DEBUG
	#ifdef RELEASE
		#define VERSION RELEASE
	#else
		#define VERSION "alpha "__DATE__
	#endif
#else
	#define VERSION "debug "__DATE__ " " __TIME__
#endif

#define SYNTH_VOICE_COUNT 6

#define SYSEX_ID_0 0x00
#define SYSEX_ID_1 0x61
#define SYSEX_ID_2 0x16

#define SYSEX_COMMAND_BANK_A 1
#define SYSEX_COMMAND_BANK_B 2
#define SYSEX_COMMAND_UPDATE_FW 0x6b

#define TICKER_1S 500
#define TEMP_BUFFER_SIZE 512

// Some constants for 16 bit ranges */
#define FULL_RANGE UINT16_MAX
#define HALF_RANGE (FULL_RANGE/2+1)
#define HALF_RANGE_L (65536UL*HALF_RANGE) // i.e. HALF_RANGE<<16, as uint32_t

////////////////////////////////////////////////////////////////////////////////
// Prophet 600 definitions
////////////////////////////////////////////////////////////////////////////////

typedef enum
{
	plSeq1=0,plSeq2=1,plArpUD=2,plArpAssign=3,plPreset=4,plRecord=5,plToTape=6,plFromTape=7,plTune=8,plDot=9
} p600LED_t;

typedef enum
{
	ppNone=-1,
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
	pcPModOscB=24,pcVolA,pcVolB,pcMVol,pcAPW,pcExtFil,pcResonance,pcBPW
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
	pbASaw=32,pbATri,pbSync,pbBSaw,pbBTri,pbPModFA,pbPModFil,pbUnison
} p600Button_t;

typedef enum
{
	modOff=0,modVCO=1,modVCF=2,modVCA=3,modPW=4
} modulation_t;

typedef enum
{
	mtNone=0,mtVCO=1,mtVCF=2,mtVCA=4,mtPW=8,mtOnlyA=16,mtOnlyB=32
} modulationTarget_t;

typedef enum
{
	smInternal=0,smMIDI=1,smTape=2
} syncMode_t;

void synth_buttonEvent(p600Button_t button, int pressed);
void synth_keyEvent(uint8_t key, int pressed);
void synth_assignerEvent(uint8_t note, int8_t gate, int8_t voice, uint16_t velocity, int8_t legato); // voice -1 is unison
void synth_uartEvent(uint8_t data);
void synth_wheelEvent(int16_t bend, uint16_t modulation, uint8_t mask, int8_t outputToMidi);
void synth_updateBender(void);
void synth_realtimeEvent(uint8_t midiEvent);

void synth_init(void);
void synth_update(void);
void synth_timerInterrupt(void);
void synth_uartInterrupt(void);

extern volatile uint32_t currentTick; // 500hz
extern uint8_t tempBuffer[TEMP_BUFFER_SIZE]; // general purpose chunk of RAM


#endif	/* SYNTH_H */

