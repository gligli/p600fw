#ifndef DISPLAY_H
#define	DISPLAY_H

#include "p600.h"

void sevenSeg_setAscii(char left, char right);
void sevenSeg_setNumber(int8_t n);

int led_getOn(p600LED_t led);
int led_getBlinking(p600LED_t led);
void led_set(p600LED_t led, int on, int blinking);

void display_init();
void display_update();


#endif	/* DISPLAY_H */

