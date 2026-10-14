#ifndef UART_6850_H
#define	UART_6850_H

#include "synth.h"

void uart_init(void);
void uart_send(uint8_t data);
void uart_update(void);

#endif	/* UART_6850_H */

