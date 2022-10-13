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

void sevenSeg_setRelative(comparator_t comparator)
{
    display_clear();
    if (comparator == comEqual)
    {
        display.sevenSegs[0]=map_to_seg7(&sevenSeg_map,*">");
        display.sevenSegs[1]=map_to_seg7(&sevenSeg_map,*"<");
    }
    else if (comparator == comGreater)
    {
        display.sevenSegs[0]=map_to_seg7(&sevenSeg_map,*"<");
    }
    else if (comparator == comLess)
    {
        display.sevenSegs[1]=map_to_seg7(&sevenSeg_map,*">");
    }
}

int led_getOn(p600LED_t led)
{
	uint16_t mask=1<<led;
	return !!(display.ledOn&mask); // return the status of the on/off bit at position LED
}

int led_getBlinking(p600LED_t led)
{
	uint16_t mask=1<<led;
	return !!(display.ledBlinking&mask); // return the status of the blinking bit at position LED
}

void led_set(p600LED_t led, int8_t on, int8_t blinking)
{
	uint16_t mask=1<<led; // set a single bit at the position of the LED

    if (!blinking) display.ledBlinking&=~mask; // deactivate blinking of the LED
    if (on)
    {
        display.ledOn|=mask; // set it blinking if flag is set
        if (blinking) display.ledBlinking|=mask;
    }
	else
	{
		display.ledOn&=~mask; // switch off the LED
    }
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
		
		// blinker, e.g. set the current state (on or off) according to the counter

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

			// this sets the ASCII characters of position and next position from the text to be displayed
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
			// if nothing to scroll, then continue to display current value or content
			localSevenSegs[0]=display.sevenSegs[0];
			localSevenSegs[1]=display.sevenSegs[1];
		}

		// this is the way the P600 hardware (the LED matrix) is built:
		// S&H in three waves,  8 bits in each wave are sent to
		// 1) 8 button LEDs (all except tune)
		// 2) the 7 segments of the left display digit + the dot
		// 3) the 7 segments of the right display digit + the tune LED
		// other parts of the display (other dots) are not connected
		// 
		// see also service manual board 1, LED matrix
		

		uint8_t b=0;

		switch (display.activeCol)
		{
		case 0:	// all the LEDs as set in the bits of ledON (according to enum p600led_t) 
				// note: this covers all "buttons" except Tune and the display dot (these are the 9th and 10th bit in .ledOn)  
			b=display.ledOn; // set the bits 
			if (display.blinkState) b^=display.ledBlinking; // deactivates the dot depending on blink state
			break;
		case 1: // left digit + the dot
			b=localSevenSegs[0]&0x7f; // 7f is the mask that has the 7 elements (and only those) activated 
			if (led_getOn(plDot)) b|=0x80; // activates the 8th bit (the display dot)
			if (led_getBlinking(plDot) && display.blinkState) b^=0x80; // deactivates the dot depending on blink state
			break;
		case 2: // right digit + tune button LED
			b=localSevenSegs[1]&0x7f; // 7f is the mask that has the 7 elements (and only those) activated 
			if (led_getOn(plTune)) b|=0x80; // activates the 8th bit (the tune button LED)
			if (led_getBlinking(plTune) && display.blinkState) b^=0x80; // deactivates the tune LED depending on blink state
			break;
		}
		
		display.activeRows[display.activeCol]=b;
	}
	
	BLOCK_INT
	{
		io_write(0x09,0x00); // switch all bits off at the address 9
		CYCLE_WAIT(1);
		io_write(0x08,0x10<<display.activeCol); // for LEDs this one bit at position 5, for left digit + dot it is 6, for right digit + tune LED it is 7
		CYCLE_WAIT(1);
		io_write(0x09,display.activeRows[display.activeCol]); // push the 8 bits into the address
	}

	display.activeCol=(display.activeCol+1)%3;
}


