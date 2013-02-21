////////////////////////////////////////////////////////////////////////////////
// Display update
////////////////////////////////////////////////////////////////////////////////

#include "display.h"
#include "map_to_7segment.h"

#define DISPLAY_BLINK_HALF_PERIOD 160
#define DISPLAY_SCROLL_RATE 80

static struct
{
	uint16_t ledOn;
	uint16_t ledBlinking;
	uint8_t activeCol;
	uint8_t sevenSegs[2];
	uint8_t blinkCounter;
	int8_t blinkState;
	
	uint8_t scrollCounter;
	int8_t scrollPos;
	int8_t scrollTimes;
	char scrollText[50];
} display;

static SEG7_DEFAULT_MAP(sevenSeg_map);

void sevenSeg_scrollText(const char * text, int8_t times)
{
	display.scrollTimes=0;
	display.scrollPos=-1;
	
	if (text && times)
	{
		display.scrollTimes=times;
		display.scrollPos=0;
		strcpy(&display.scrollText[1],text);
		strcat(display.scrollText," ");
	}
}

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

void led_set(p600LED_t led, int8_t on, int8_t blinking)
{
	uint16_t mask=1<<led;
	
	display.ledOn&=~mask;
	display.ledBlinking&=~mask;
	
	if (on) display.ledOn|=mask;
	if (blinking) display.ledBlinking|=mask;
}

void display_clear()
{
	display.sevenSegs[0]=0;
	display.sevenSegs[1]=0;
	display.ledOn=0;
	display.ledBlinking=0;
}

void display_init()
{
	memset(&display,0,sizeof(display));
	display.scrollText[0]=' ';
}

void display_update()
{
	// blinker
	
	display.blinkCounter++;
	
	if (display.blinkCounter>DISPLAY_BLINK_HALF_PERIOD)
	{
		display.blinkState=!display.blinkState;
		display.blinkCounter=0;
	}
	
	// scroller
	
	display.scrollCounter++;
	
	if (display.scrollCounter>DISPLAY_SCROLL_RATE &&  display.scrollTimes)
	{
		int8_t l,p,p2;

		l=strlen(display.scrollText);
		p=display.scrollPos;
		p2=(display.scrollPos+1)%l;

		sevenSeg_setAscii(display.scrollText[p],display.scrollText[p2]);

		display.scrollPos=p2;
		display.scrollCounter=0;
		
		if(p2==0 && display.scrollTimes>0)
			--display.scrollTimes;
	}
	
	// update one third of display at a time
	
	uint8_t b=0;
	
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
	
	BLOCK_INT
	{
		io_write(0x09,0x00);
		io_write(0x08,0x10<<display.activeCol);
		CYCLE_WAIT(4);
		io_write(0x09,b);
	}
	
	display.activeCol=(display.activeCol+1)%3;
}


