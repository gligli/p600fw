////////////////////////////////////////////////////////////////////////////////
// Top level
////////////////////////////////////////////////////////////////////////////////

#include <string.h>

#include "p600.h"

#include "display.h"
#include "scanner.h"
#include "synth.h"

void p600_init(void)
{
	print("p600mockup\n");
	
	display_init();
	scanner_init();
	synth_init();
	
	sevenSeg_setNumber(42);
}

void p600_main(void)
{
//	print("tick\n");
	
	led_set(plSeq1,0,0);
	led_set(plSeq2,1,0);
	led_set(plArpUD,1,1);
	led_set(plArpAssign,1,1);
	
	uint32_t i;
	static uint32_t tick=0;
	
	for(i=0;i<32;++i)
		synth_setCV(i,i*1000+tick);
	++tick;
	
	display_update();
	scanner_update();
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
	led_set(plToTape,pressed,0);
}

