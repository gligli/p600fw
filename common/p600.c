////////////////////////////////////////////////////////////////////////////////
// Top level code
////////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "p600.h"

#include "scanner.h"
#include "display.h"
#include "synth.h"
#include "potmux.h"
#include "adsr.h"

#define P600_VOICE_COUNT 6

static struct
{
	struct adsr_s filEnvs[P600_VOICE_COUNT];
	struct adsr_s ampEnvs[P600_VOICE_COUNT];
} p600;

void p600_init(void)
{
	scanner_init();
	display_init();
	synth_init();
	potmux_init();

	int8_t i;
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_init(&p600.ampEnvs[i]);
		adsr_init(&p600.filEnvs[i]);
	}
	
	led_set(plSeq1,0,0);
	led_set(plSeq2,1,0);
	led_set(plArpUD,1,1);
	led_set(plArpAssign,0,1);
	
	synth_update();
}

void p600_update(void)
{
	// must scan before displaying, because scanning clears display
	scanner_update();
	display_update();
	
	int8_t i;
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_setCVs(&p600.ampEnvs[i],potmux_getValue(ppAmpAtt),potmux_getValue(ppAmpDec),potmux_getValue(ppAmpSus),potmux_getValue(ppAmpRel),UINT16_MAX);
		adsr_setCVs(&p600.filEnvs[i],potmux_getValue(ppFilAtt),potmux_getValue(ppFilDec),potmux_getValue(ppFilSus),potmux_getValue(ppFilRel),potmux_getValue(ppFilEnvAmt));
	}

	synth_setCV(pcMVol,potmux_getValue(ppMVol),1);
	synth_setCV(pcVolA,potmux_getValue(ppMixer),1);
	synth_setCV(pcVolB,potmux_getValue(ppGlide),1);
	synth_setCV(pcAPW,potmux_getValue(ppAPW),1);
	synth_setCV(pcBPW,potmux_getValue(ppBPW),1);
	synth_setCV(pcRes,potmux_getValue(ppResonance),1);
	
	synth_setCV(pcOsc1A,potmux_getValue(ppFreqA),1);
	synth_setCV(pcOsc1B,potmux_getValue(ppFreqB),1);
	
	potmux_update();
}

void p600_interrupt(void)
{
	int8_t i;
	
	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_update(&p600.ampEnvs[i]);
		adsr_update(&p600.filEnvs[i]);
	}

	synth_setCV(pcAmp1,adsr_getOutput(&p600.ampEnvs[0]),1);
	synth_setCV(pcFil1,adsr_getOutput(&p600.filEnvs[0])+potmux_getValue(ppCutoff),1);
}


void p600_buttonEvent(p600Button_t button, int pressed)
{
	int8_t i;

	sevenSeg_setNumber(button);
	led_set(plToTape,pressed,0);
	
	switch(button)
	{
	case pbASaw:
		synth_setGate(pgASaw,pressed,1);
		break;
	case pbBSaw:
		synth_setGate(pgBSaw,pressed,1);
		break;
	case pbATri:
		synth_setGate(pgATri,pressed,1);
		break;
	case pbBTri:
		synth_setGate(pgBTri,pressed,1);
		break;
	case pbASqr:
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setShape(&p600.filEnvs[i],pressed);
		break;
	case pbBSqr:
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setShape(&p600.ampEnvs[i],pressed);
		break;
	case pbFromTape:
		for(i=0;i<P600_VOICE_COUNT;++i)
			adsr_setGate(&p600.ampEnvs[i],pressed);
		break;
	default:
		;
	}
	
}

void p600_keyEvent(uint8_t key, int pressed)
{
	int8_t i;

	sevenSeg_setNumber(key);
	led_set(plFromTape,pressed,0);

	for(i=0;i<P600_VOICE_COUNT;++i)
	{
		adsr_setGate(&p600.ampEnvs[i],pressed);
		adsr_setGate(&p600.filEnvs[i],pressed);
	}
}

