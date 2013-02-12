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

struct adsr_s fenv;
struct adsr_s aenv;

void p600_init(void)
{
	print("p600fw\n");
	
	scanner_init();
	display_init();
	synth_init();
	potmux_init();

	adsr_init(&fenv);
	adsr_init(&aenv);
	
	sevenSeg_setAscii('H','i');
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
	
	adsr_setCVs(&aenv,potmux_getValue(ppAmpAtt),potmux_getValue(ppAmpDec),potmux_getValue(ppAmpSus),potmux_getValue(ppAmpRel),UINT16_MAX);
	adsr_setCVs(&fenv,potmux_getValue(ppFilAtt),potmux_getValue(ppFilDec),potmux_getValue(ppFilSus),potmux_getValue(ppFilRel),potmux_getValue(ppFilEnvAmt));

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
	adsr_update(&aenv);
	adsr_update(&fenv);

	synth_setCV(pcAmp1,adsr_getOutput(&aenv),1);
	synth_setCV(pcFil1,adsr_getOutput(&fenv)+potmux_getValue(ppCutoff),1);
}


void p600_buttonEvent(p600Button_t button, int pressed)
{
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
		adsr_setShape(&fenv,pressed);
		break;
	case pbBSqr:
		adsr_setShape(&aenv,pressed);
		break;
	default:
		;
	}
	
}

void p600_keyEvent(uint8_t key, int pressed)
{
	sevenSeg_setNumber(key);
	led_set(plFromTape,pressed,0);

	adsr_setGate(&aenv,pressed);
	adsr_setGate(&fenv,pressed);
}

