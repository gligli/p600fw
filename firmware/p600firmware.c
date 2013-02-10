#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include "usb_debug_only.h"
#include "print.h"

#include "p600.h"

#define CPU_PRESCALE(n)	(CLKPR = 0x80, CLKPR = (n))
#define CPU_16MHz       0x00
#define CPU_8MHz        0x01
#define CPU_4MHz        0x02
#define CPU_2MHz        0x03
#define CPU_1MHz        0x04
#define CPU_500kHz      0x05
#define CPU_250kHz      0x06
#define CPU_125kHz      0x07
#define CPU_62kHz       0x08

#define CYCLE_WAIT asm volatile("nop\n nop\n nop\n nop\n");

#define DO_ONE_WAIT(x) \
	CYCLE_WAIT \
	if (cycles<x) return; // this test is 3 ops + nop, so that's about a 4Mhz period

#define FORCEINLINE __attribute__((always_inline))

void wait(uint8_t cycles)
{
	DO_ONE_WAIT(0x01);
	DO_ONE_WAIT(0x02);
	DO_ONE_WAIT(0x03);
	DO_ONE_WAIT(0x04);
	DO_ONE_WAIT(0x05);
	DO_ONE_WAIT(0x06);
	DO_ONE_WAIT(0x07);
	DO_ONE_WAIT(0x08);
	DO_ONE_WAIT(0x09);
	DO_ONE_WAIT(0x0a);
	DO_ONE_WAIT(0x0b);
	DO_ONE_WAIT(0x0c);
	DO_ONE_WAIT(0x0d);
	DO_ONE_WAIT(0x0e);
	DO_ONE_WAIT(0x0f);
	DO_ONE_WAIT(0x10);
	DO_ONE_WAIT(0x11);
	DO_ONE_WAIT(0x12);
	DO_ONE_WAIT(0x13);
	DO_ONE_WAIT(0x14);
	DO_ONE_WAIT(0x15);
	DO_ONE_WAIT(0x16);
	DO_ONE_WAIT(0x17);
	DO_ONE_WAIT(0x18);
	DO_ONE_WAIT(0x19);
	DO_ONE_WAIT(0x1a);
	DO_ONE_WAIT(0x1b);
	DO_ONE_WAIT(0x1c);
	DO_ONE_WAIT(0x1d);
	DO_ONE_WAIT(0x1e);
	DO_ONE_WAIT(0x1f);
	DO_ONE_WAIT(0x20);
}

void hardware_init(void)
{
	// LSB->MSB
	// B: A3-A9,A12
	// C: D7,D0,D1,INT,NMI,Halt,MREQ,IORQ
	// D: A13-A15,CLK,D4,D3,D5,D6
	// E: nc,D2,nc,nc,nc,A0-A2
	// F: Rfsh,M1,Reset,BUSREQ,Wait,BusAck,WR,RD
	
	DDRB=0;
	DDRC=0;
	DDRD=0;
	DDRE=0;
	DDRF=0;

	PORTB=0b00000000;
	PORTC=0b11100000;
	PORTD=0b00000000;
	PORTE=0b00000000;
	PORTF=0b11100011;
	
	DDRB=0b11111111;
	DDRC=0b11100000;
	DDRD=0b00000111;
	DDRE=0b11100000;
	DDRF=0b11100011;
}

FORCEINLINE void hardware_clearFlags(void)
{
	// no r,w,mreq,ioreq for now

	PORTC|=0xc0;
	PORTF|=0xc0;
}

FORCEINLINE void hardware_prepareRW(uint16_t addr,int write, int io)
{
	uint8_t pb=0,pc=0,pd=0,pe=0,pf=0;
	
	// set address
	
	pb|=(addr>> 3)&0x7f;
	pb|=(addr>> 5)&0x80;

	pd|=(addr>>13)&0x07;
	
	pe|=(addr<< 5)&0xe0;
	
	PORTB=pb;
	PORTD=(PORTD&0xf8)|pd;
	PORTE=(PORTE&0x1f)|pe;

	// set data dir
	
	if (write)
	{
		DDRC|=0b00000111;
		DDRD|=0b11110000;
		DDRE|=0b00000010;
	}
	else
	{
		DDRC&=~0b00000111;
		DDRD&=~0b11110000;
		DDRE&=~0b00000010;
	}

	// set flags
	
	pc|=(io)?0x40:0x80; // MREQ,IORQ are active low
	pf|=(write)?0x80:0x40; // RD,WR are active low
	
	PORTC=(PORTC&0x3f)|pc;
	PORTF=(PORTF&0x3f)|pf;
}

FORCEINLINE void hardware_write(uint8_t data)
{
	uint8_t pc=0,pd=0,pe=0;
	
	pc|=(data>>7)&0x01;
	pc|=(data<<1)&0x06;
	
	pd|=(data   )&0x10;
	pd|=(data<<2)&0x20;
	pd|=(data<<1)&0xc0;
	
	pe|=(data>>1)&0x02;
	
	PORTC=(PORTC&0xf8)|pc;
	PORTD=(PORTD&0x0f)|pd;
	PORTE=(PORTE&0xfd)|pe;
}

FORCEINLINE uint8_t hardware_read(void)
{
	uint8_t pc,pd,pe,v=0;
	
	pc=PINC;
	pd=PIND;
	pe=PINE;
	
	v|=(pc<<7)&0x80;
	v|=(pc>>1)&0x03;
	
	v|=(pd	 )&0x10;
	v|=(pd>>2)&0x08;
	v|=(pd>>1)&0x60;
	
	v|=(pe<<1)&0x04;
	
	return v;
}

FORCEINLINE void mem_write(uint16_t address, uint8_t value)
{
	hardware_clearFlags();
	hardware_write(value);
	CYCLE_WAIT;
	hardware_prepareRW(address,1,0);
}

FORCEINLINE void io_write(uint8_t address, uint8_t value)
{
	hardware_clearFlags();
	hardware_write(value);
	CYCLE_WAIT;
	hardware_prepareRW(address,1,1);
}

FORCEINLINE uint8_t mem_read(uint16_t address)
{
	hardware_clearFlags();
	hardware_prepareRW(address,0,0);
	CYCLE_WAIT;
	return hardware_read();
}

FORCEINLINE uint8_t io_read(uint8_t address)
{
	hardware_clearFlags();
	hardware_prepareRW(address,0,1);
	CYCLE_WAIT;
	return hardware_read();
}

int main(void)
{
	CPU_PRESCALE(CPU_16MHz);

	// initialize the USB, and then wait for the host
	// to set configuration.  If the Teensy is powered
	// without a PC connected to the USB port, this 
	// will wait forever.
	usb_init();
	while (!usb_configured()) /* wait */ ;

	// wait an extra second for the PC's operating system
	// to load drivers and do whatever it does to actually
	// be ready for input
	_delay_ms(1000);

	print("p600firmware\n");
	
	hardware_init();
	p600_init();
	
	print("loop\n");
	for(;;)
	{
//		_delay_ms(50);
		p600_update();
	}
}
