#ifndef SCANNER_H
#define	SCANNER_H

#include "p600.h"

int8_t scanner_keyState(uint8_t key);
int8_t scanner_buttonState(p600Button_t button);

void scanner_init(void);
void scanner_update(void);

#endif	/* SCANNER_H */

