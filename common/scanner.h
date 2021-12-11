#ifndef SCANNER_H
#define	SCANNER_H

#include "synth.h"

#define SCANNER_BASE_NOTE 36
#define SCANNER_C2 (SCANNER_BASE_NOTE+24)
#define SCANNER_C5 (SCANNER_BASE_NOTE+60)
#define SCANNER_B4 (SCANNER_BASE_NOTE+59)
#define SCANNER_Bb4 (SCANNER_BASE_NOTE+58)

int8_t scanner_keyState(uint8_t key);
int8_t scanner_buttonState(p600Button_t button);

void scanner_init(void);
void scanner_update(int8_t fullScan);
int8_t scanner_isKeyDown(uint8_t note);

#endif	/* SCANNER_H */

