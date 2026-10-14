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
	uint8_t columnLatch;	// what display_update last wrote to the CSO0 latch (the enable bit of the active column); the scanner uses the same latch, see display_restoreColumn()

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
		io_write(CSO1, 0x00); // switch all bits off at the address 9
		CYCLE_WAIT(1);
		display.columnLatch=0x10<<display.activeCol; // remembered so that it can be written again after the scanner has used this latch
		io_write(CSO0, display.columnLatch); // for LEDs this one bit at position 5, for left digit + dot it is 6, for right digit + tune LED it is 7
		CYCLE_WAIT(1);
		io_write(CSO1, display.activeRows[display.activeCol]); // push the 8 bits into the address
	}

	display.activeCol=(display.activeCol+1)%3;
}

// The scanner selects the byte to read by writing the low bits of the same CSO0 latch that holds the display column enable
// (the high bits), so every scanner read switches the active column off. The column has to be switched on again afterwards,
// otherwise it only stays on until the next scanner read, and when the scanner reads a little on every tick that means
// the display and the LEDs are dark almost all the time. This writes back what display_update last wrote (the rows are in
// another latch and are not touched by the scanner, so they are still there).
// A write does not reach the latch by itself: the write routine leaves /WR low, and the latch only takes the value when /WR goes
// high again at the start of the NEXT bus access, which can be thousands of cycles away. So the write is followed by a read of the
// key matrix (the same read the scanner does all the time, it changes nothing); that read is the bus access that completes the write.
void display_restoreColumn(void)
{
	BLOCK_INT
	{
		io_write(CSO0, display.columnLatch);
		io_read(CSI1); // result not needed, see above (CSI1 = CSO2 = 0x0A, the address the scanner reads)
	}
}

////////////////////////////////////////////////////////////////////////////////
// Power-up light show (about 3.2 seconds)
////////////////////////////////////////////////////////////////////////////////

// The button LEDs as they sit on the front panel, one mask per row (the bits are the p600LED_t numbers, as in display.ledOn):
// To Tape + From Tape / Tune / Preset + Record / Seq 1 + Seq 2 + Arp U/D + Arp Assign
static const uint16_t showRows[4] PROGMEM =
{
	(1<<plToTape)|(1<<plFromTape),
	(1<<plTune),
	(1<<plPreset)|(1<<plRecord),
	(1<<plSeq1)|(1<<plSeq2)|(1<<plArpUD)|(1<<plArpAssign)
};

// The 9 button LEDs in reading order (top row first, left to right): the path of the running light.
// (This assumes the panel order is the one in the row list above; to change the order of the light, change it here.)
static const uint8_t showPath[9] PROGMEM = { plToTape, plFromTape, plTune, plPreset, plRecord, plSeq1, plSeq2, plArpUD, plArpAssign };

#define SHOW_ALL 0x3ff // all 10 LED bits: the 8 button LEDs, Tune and the display dot

// One step of the show is one complete refresh of the display: 3 columns of 4 ms, so 12 ms. Every pattern is held for a number of steps.
// This is called with interrupts off (from synth_init), so the display is refreshed here by calling display_update() directly,
// the same way the tuner does it while it is running.
static void showFrame(uint16_t leds, uint8_t segLeft, uint8_t segRight, uint8_t steps)
{
	uint8_t s,c;

	display.ledOn=leds;
	display.ledBlinking=0;
	display.sevenSegs[0]=segLeft;
	display.sevenSegs[1]=segRight;

	for(s=0;s<steps;++s)
	{
		for(c=0;c<3;++c)
		{
			display_update(1);
			// V2.5: a write to the display only reaches its latch when the NEXT bus access starts (see display_restoreColumn), so
			// without this read the last write (the segments/LEDs of the column) arrived only at the next display_update, just before
			// it was switched off again: the light show stayed dark. The read of the key matrix completes the write and changes nothing.
			BLOCK_INT
			{
				io_read(CSI1);
			}
			// 4 ms = 64000 clock cycles at 16 MHz. CYCLE_WAIT(x) waits 4*x cycles and the 4*x must fit into a 16 bit int (max 32767),
			// so it is done in two halves (CYCLE_WAIT(16000) would overflow and give a wrong, much shorter delay)
			CYCLE_WAIT(8000);
			CYCLE_WAIT(8000);
		}
	}
}

// Two neighbouring segments of the outer ring (a,b,c,d,e,f = bits 0..5) of a digit: the head at ring position pos (any number, it wraps
// around after 6) and the one behind it. Running backwards puts the tail on the other side.
static uint8_t showRing(uint8_t pos, uint8_t backwards)
{
	uint8_t head=pos%6;
	uint8_t tail=backwards?((head+1)%6):((head+5)%6);

	return (1<<head)|(1<<tail);
}

// Row for step k of the waterfall: 0,1,2,3 going down, then 3,2,1,0 going back up
static uint8_t showRowIdx(uint8_t k)
{
	return (k<4)?k:(7-k);
}

void display_bootShow(void)
{
	uint8_t n=0; // frame counter, it moves the ring of segments that runs around the two digits (in opposite directions)
	uint8_t k,sweep,steps;
	uint16_t leds;

	// Act 1, waterfall (8 frames of 96 ms): a bar of LEDs rows runs down the panel and back up, with the row it came from still lit

	for(k=0;k<8;++k)
	{
		leds=pgm_read_word(&showRows[showRowIdx(k)]);
		if(k>0)
			leds|=pgm_read_word(&showRows[showRowIdx(k-1)]);
		showFrame(leds,showRing(n,0),showRing(5-(n%6),1),8);
		++n;
	}

	// Act 2, running light (3 sweeps of 9 LEDs): one LED with a tail runs through all button LEDs in reading order and back, and
	// again, faster each time (60, 48 and 36 ms per LED)

	for(sweep=0;sweep<3;++sweep)
	{
		steps=5-sweep;
		for(k=0;k<9;++k)
		{
			// forward, backward, forward again: the LED that is lit and the one it just left (the tail)
			leds=(uint16_t)1<<pgm_read_byte(&showPath[(sweep&1)?(8-k):k]);
			if(k>0)
				leds|=(uint16_t)1<<pgm_read_byte(&showPath[(sweep&1)?(9-k):(k-1)]);
			showFrame(leds,showRing(n,0),showRing(5-(n%6),1),steps);
			++n;
		}
	}

	// Act 3, fill and flash: the rows fill up from the bottom (4 frames of 96 ms) while both digits light up completely, then everything
	// stays on for 192 ms (all LEDs, the Tune LED, "8.8." with the dot) and flashes off and on twice (96 ms each)

	leds=0;
	for(k=0;k<4;++k)
	{
		leds|=pgm_read_word(&showRows[3-k]);
		showFrame(leds,0x7f,0x7f,8);
	}

	showFrame(SHOW_ALL,0x7f,0x7f,16);

	for(k=0;k<2;++k)
	{
		showFrame(0,0,0,8);
		showFrame(SHOW_ALL,0x7f,0x7f,8);
	}

	// Act 4, hand over: dark for 192 ms, then the normal start goes on (and the welcome text scrolls)

	showFrame(0,0,0,16);

	display_clear();
}

