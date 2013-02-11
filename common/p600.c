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
}

void p600_update(void)
{
	uint32_t i;
	for(i=0;i<32;++i)
	{
		synth_setCV(i,(i>16&&i<22)?0:potmux_getValue(i));
	}
	
	adsr_setCVs(&aenv,potmux_getValue(ppAmpAtt),potmux_getValue(ppAmpDec),potmux_getValue(ppAmpSus),potmux_getValue(ppAmpRel),UINT16_MAX);
	adsr_setCVs(&fenv,potmux_getValue(ppFilAtt),potmux_getValue(ppFilDec),potmux_getValue(ppFilSus),potmux_getValue(ppFilRel),potmux_getValue(ppFilEnvAmt));

	adsr_update(&aenv);
	adsr_update(&fenv);
	
	synth_setCV(pcAmp5,adsr_getOutput(&aenv));
	synth_setCV(pcFil5,adsr_getOutput(&fenv));
	
	// must scan before displaying, because scanning clears display
	scanner_update();
	display_update();
	
	potmux_update();
	synth_update();
}

void p600_buttonEvent(p600Button_t button, int pressed)
{
	sevenSeg_setNumber(button);
	led_set(plToTape,pressed,0);
	
	if(button==pbASqr) adsr_setShape(&fenv,pressed);
	if(button==pbBSqr) adsr_setShape(&aenv,pressed);
	if(button==pbFromTape) adsr_setGate(&aenv,pressed);
	if(button==pbToTape) adsr_setGate(&fenv,pressed);
}

void p600_keyEvent(uint8_t key, int pressed)
{
	sevenSeg_setNumber(key);
	led_set(plFromTape,pressed,0);
}

