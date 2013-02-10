////////////////////////////////////////////////////////////////////////////////
// Top level code
////////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "p600.h"

#include "scanner.h"
#include "display.h"
#include "synth.h"
#include "potmux.h"

void p600_init(void)
{
	print("p600fw\n");
	
	scanner_init();
	display_init();
	synth_init();
	potmux_init();
	
	sevenSeg_setAscii('H','i');
	led_set(plSeq1,0,0);
	led_set(plSeq2,1,0);
	led_set(plArpUD,1,1);
	led_set(plArpAssign,1,1);
}

void p600_update(void)
{
	uint32_t i;
	for(i=0;i<32;++i)
	{
		synth_setCV(i,(i>16&&i<22)?0:potmux_getValue(i));
	}
	
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
}

void p600_keyEvent(uint8_t key, int pressed)
{
	sevenSeg_setNumber(key);
	led_set(plFromTape,pressed,0);
}

