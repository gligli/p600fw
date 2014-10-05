#ifndef ARP_H
#define	ARP_H

#include "synth.h"

typedef enum
{
	amOff=0,amUpDown=1,amRandom=2,amAssign=3
} arpMode_t;


void arp_init(void);
void arp_update(void);

void arp_setMode(arpMode_t mode, int8_t hold);  // hold: 0 = not held; 1 = hold pressed and future presses; 2 = hold only pressed
void arp_setSpeed(uint16_t speed);
void arp_setTranspose(int8_t transpose);
arpMode_t arp_getMode(void);
int8_t arp_getHold(void);
void arp_resetCounter(void);

void arp_assignNote(uint8_t note, int8_t on);

#endif	/* ARP_H */

