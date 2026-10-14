#ifndef DISPLAY_H
#define	DISPLAY_H

#include "synth.h"

typedef enum
{
    comEqual=0,
    comGreater=2,
    comLess=3,

} comparator_t;

void sevenSeg_scrollText(const char * text, int8_t times); // -1 -> infinite / 0 -> off
void sevenSeg_setAscii(char left, char right);
void sevenSeg_setNumber(int32_t n);
void sevenSeg_setRelative(comparator_t comparator);

int led_getOn(p600LED_t led);
int led_getBlinking(p600LED_t led);
void led_set(p600LED_t led, int8_t on, int8_t blinking);

void display_clear(void);

void display_init(void);
void display_update(int8_t fullUpdate);
void display_bootShow(void); // the power-up LED light show (about 3.2 seconds, blocking, call with interrupts off)
void display_restoreColumn(void); // call after anything else has used the CSO0 latch (the scanner), to switch the display column on again


#endif	/* DISPLAY_H */

