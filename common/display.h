#ifndef DISPLAY_H
#define	DISPLAY_H

#include "p600.h"

void sevenSeg_setAscii(char left, char right);
void sevenSeg_setNumber(int8_t n);

int led_getOn(p600LED_t led);
int led_getBlinking(p600LED_t led);
void led_set(p600LED_t led, int8_t on, int8_t blinking);

void display_init(void);
void display_update(void);


#endif	/* DISPLAY_H */

