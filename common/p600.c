////////////////////////////////////////////////////////////////////////////////
// Top level
////////////////////////////////////////////////////////////////////////////////

#include <string.h>
#include "p600.h"

void p600_init(void)
{
	print("p600mockup\n");
	
	display_init();
	scanner_init();
	
	sevenSeg_setNumber(42);
}

void p600_main(void)
{
//	print("tick\n");
	
	led_set(plSeq1,0,0);
	led_set(plSeq2,1,0);
	led_set(plArpUD,1,1);
	led_set(plArpAssign,1,1);
	
	display_update();
	scanner_update();
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

