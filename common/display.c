////////////////////////////////////////////////////////////////////////////////
// Display update
////////////////////////////////////////////////////////////////////////////////

#include "display.h"
#include "map_to_7segment.h"

#define DISPLAY_BLINK_HALF_PERIOD 60 // 300ms

static struct
{
	uint8_t activeCol;
	uint8_t sevenSegs[2];
	uint16_t ledOn;
	uint16_t ledBlinking;
	uint16_t blinkCounter;
	int blinkState;
} display;

static SEG7_DEFAULT_MAP(sevenSeg_map);

void sevenSeg_setAscii(char left, char right)
{
	display.sevenSegs[0]=map_to_seg7(&sevenSeg_map,left);
	display.sevenSegs[1]=map_to_seg7(&sevenSeg_map,right);
}

void sevenSeg_setNumber(int8_t n)
{
	n%=100;
	sevenSeg_setAscii('0'+(n/10),'0'+(n%10));
}

int led_getOn(p600LED_t led)
{
	uint16_t mask=1<<led;
	return !!(display.ledOn&mask);
}

int led_getBlinking(p600LED_t led)
{
	uint16_t mask=1<<led;
	return !!(display.ledBlinking&mask);
}

void led_set(p600LED_t led, int on, int blinking)
{
	uint16_t mask=1<<led;
	
	display.ledOn&=~mask;
	display.ledBlinking&=~mask;
	
	if (on) display.ledOn|=mask;
	if (blinking) display.ledBlinking|=mask;
}

void display_init()
{
	memset(&display,0,sizeof(display));
}

void display_update()
{
	display.blinkCounter++;
	
	if (display.blinkCounter>DISPLAY_BLINK_HALF_PERIOD)
	{
		display.blinkState=!display.blinkState;
		display.blinkCounter=0;
	}
	
	// update one third of display at a time
	
	uint8_t b;
	
	switch (display.activeCol)
	{
	case 0:
		b=display.ledOn;
		if (display.blinkState) b^=display.ledBlinking;
		break;
	case 1:
		b=display.sevenSegs[0]&0x7f;
		if (led_getOn(plDot)) b|=0x80;
		break;
	case 2:
		b=display.sevenSegs[1]&0x7f;
		if (led_getOn(plTune)) b|=0x80;
		break;
	}
	
		
	io_write(0x09,0x00);
	io_write(0x08,0x10<<display.activeCol);
	io_write(0x09,b);
	
	display.activeCol=(display.activeCol+1)%3;
}


