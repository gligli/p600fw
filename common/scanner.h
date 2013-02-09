#ifndef SCANNER_H
#define	SCANNER_H

#include "p600.h"

int scanner_keyState(uint8_t key);
int scanner_buttonState(p600Button_t button);

void scanner_init(void);
void scanner_update(void);

#endif	/* SCANNER_H */

