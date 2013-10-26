#ifndef SCANNER_H
#define	SCANNER_H

#include "synth.h"

int8_t scanner_keyState(uint8_t key);
int8_t scanner_buttonState(p600Button_t button);

void scanner_init(void);
void scanner_update(int8_t fullScan);

#endif	/* SCANNER_H */

