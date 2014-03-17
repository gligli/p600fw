////////////////////////////////////////////////////////////////////////////////
// Display update
////////////////////////////////////////////////////////////////////////////////

#include "display.h"
#include "map_to_7segment.h"

#define DISPLAY_BLINK_HALF_PERIOD 20
#define DISPLAY_SCROLL_RATE 15

static struct
{
	uint16_t ledOn;
	uint16_t ledBlinking;
	
	uint8_t sevenSegs[2];
	
	uint8_t blinkCounter;
	int8_t blinkState;
	
	uint8_t scrollCounter;
	int8_t scrollPos;
	int8_t scrollTimes;

	uint8_t activeCol;
	uint8_t activeRows[3];

	char scrollText[50];
} display;

static SEG7_DEFAULT_MAP(sevenSeg_map);

void LOWERCODESIZE sevenSeg_scrollText(const char * text, int8_t times)
{
	display.scrollTimes=times;
	display.scrollPos=-1;
	
	if (text)
	{
		display.scrollTimes=times;
		display.scrollPos=0;
		strcpy(&display.scrollText[1],text);
		strcat(display.scrollText," ");
	}
}

void LOWERCODESIZE sevenSeg_setAscii(char left, char right)
{
	display.sevenSegs[0]=map_to_seg7(&sevenSeg_map,left);
	display.sevenSegs[1]=map_to_seg7(&sevenSeg_map,right);
}

void LOWERCODESIZE sevenSeg_setNumber(int32_t n)
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
	display.scrollTimes=0;
}

void display_init()
{
	memset(&display,0,sizeof(display));
	display.scrollText[0]=' ';
}

void display_update(int8_t fullUpdate)
{
	if(fullUpdate)
	{
		uint8_t localSevenSegs[2];
		
		// blinker

		display.blinkCounter++;

		if (display.blinkCounter>DISPLAY_BLINK_HALF_PERIOD)
		{
			display.blinkState=!display.blinkState;
			display.blinkCounter=0;
		}

		// scroller

		if(display.scrollTimes)
		{
			int8_t l,p,p2;

			display.scrollCounter++;

			l=strlen(display.scrollText);
			p=display.scrollPos;
			p2=(display.scrollPos+1)%l;

			localSevenSegs[0]=map_to_seg7(&sevenSeg_map,display.scrollText[p]);
			localSevenSegs[1]=map_to_seg7(&sevenSeg_map,display.scrollText[p2]);

			if (display.scrollCounter>DISPLAY_SCROLL_RATE)
			{
				display.scrollPos=p2;
				display.scrollCounter=0;

				if(p2==0 && display.scrollTimes>0)
					--display.scrollTimes;
			}
		}
		else
		{
			localSevenSegs[0]=display.sevenSegs[0];
			localSevenSegs[1]=display.sevenSegs[1];
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
			b=localSevenSegs[0]&0x7f;
			if (led_getOn(plDot)) b|=0x80;
			if (led_getBlinking(plDot) && display.blinkState) b^=0x80;
			break;
		case 2:
			b=localSevenSegs[1]&0x7f;
			if (led_getOn(plTune)) b|=0x80;
			if (led_getBlinking(plTune) && display.blinkState) b^=0x80;
			break;
		}
		
		display.activeRows[display.activeCol]=b;
	}
	
	BLOCK_INT
	{
		io_write(0x09,0x00);
		CYCLE_WAIT(1);
		io_write(0x08,0x10<<display.activeCol);
		CYCLE_WAIT(1);
		io_write(0x09,display.activeRows[display.activeCol]);
	}

	display.activeCol=(display.activeCol+1)%3;
}


