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

static uint8_t prevWrite=0;

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
	
	// prepare a 200hz interrupt
/*	
	OCR1A=10000;
	TCCR1B|=(1<<WGM12)|(1<<CS11);  //Timer 1 prescaler = 8, Clear-Timer on Compare (CTC) 
	TIMSK1|=(1<<OCIE1A);//Enable overflow interrupt for Timer1
*/
	// prepare a 2Khz interrupt
	
	OCR2A=125;
	TCCR2A|=(1<<WGM21); //Timer 2 Clear-Timer on Compare (CTC) 
	TCCR2B|=(1<<CS22);  //Timer 2 prescaler = 64
	TIMSK2|=(1<<OCIE2A);//Enable overflow interrupt for Timer2
}

static void hardware_write(int8_t io, uint16_t addr, uint8_t data)
{
	uint8_t b,c,d,e,f;
	
	// no r,w,mreq,ioreq for now

	PORTF=0xc6;
	PORTC=0xc0;

	// data dir
	
	if(!prevWrite)
	{
		DDRC=0b11100111;
		DDRD=0b11110111;
		DDRE=0b11100010;
		prevWrite=1;
	}		
	
	// flags
	
	b=0x00;
	c=(io)?0x40:0x80;
	d=0x00;
	e=0x00;
	f=0x87;
	
	// address
	
	b|=(addr>> 3)&0x7f;
	b|=(addr>> 5)&0x80;
	
	d|=(addr>>13)&0x07;

	e|=(addr<< 5)&0xe0;

	// data
	
	c|=(data>>7)&0x01;
	c|=(data<<1)&0x06;
	
	d|=(data   )&0x10;
	d|=(data<<2)&0x20;
	d|=(data<<1)&0xc0;
	
	e|=(data>>1)&0x02;

	// output it
	
	PORTB=b;
	PORTC=c;
	PORTD=d;
	PORTE=e;
	PORTF=f;
}

static uint8_t hardware_read(int8_t io, uint16_t addr)
{
	uint8_t b,c,d,e,f,v;
	
	// no r,w,mreq,ioreq for now

	PORTF=0xc6;
	PORTC=0xc0;

	// prepare read
	
	if(prevWrite)
	{
		DDRC=0b11100000;
		DDRD=0b00000111;
		DDRE=0b11100000;
		prevWrite=0;
	}
	
	// flags
	
	b=0x00;
	c=(io)?0x40:0x80;
	d=0x00;
	e=0x00;
	f=0x47;
	
	// address
	
	b|=(addr>> 3)&0x7f;
	b|=(addr>> 5)&0x80;
	
	d|=(addr>>13)&0x07;

	e|=(addr<< 5)&0xe0;

	// output it
	
	PORTB=b;
	PORTC=c;
	PORTD=d;
	PORTE=e;
	PORTF=f;

	// wait
	
	CYCLE_WAIT(1);
	
	// read data
	
	c=PINC;
	d=PIND;
	e=PINE;
	
	v =(c<<7)&0x80;
	v|=(c>>1)&0x03;
	
	v|=(d	 )&0x10;
	v|=(d>>2)&0x08;
	v|=(d>>1)&0x60;
	
	v|=(e<<1)&0x04;
	
	return v;
}

void mem_write(uint16_t address, uint8_t value)
{
	hardware_write(0,address,value);
}

void io_write(uint8_t address, uint8_t value)
{
	hardware_write(1,address,value);
}

uint8_t mem_read(uint16_t address)
{
	return hardware_read(0,address);
}

uint8_t io_read(uint8_t address)
{
	return hardware_read(1,address);
}

int main(void)
{
	// initialize clock
	
	CPU_PRESCALE(CPU_125kHz); // power supply still ramping up voltage
	_delay_ms(1); // actual delay 128 ms when F_OSC is 16000000
	CPU_PRESCALE(CPU_16MHz);  

	// initialize firmware
	
	cli();
	
	hardware_init();
	p600_init();

#ifdef DEBUG
	// initialize the USB, and then wait for the host
	// to set configuration.  If the Teensy is powered
	// without a PC connected to the USB port, this 
	// will wait forever.
	usb_init();
	while (!usb_configured()) /* wait */ ;

	// wait an extra second for the PC's operating system
	// to load drivers and do whatever it does to actually
	// be ready for input
	_delay_ms(500);
#endif
	
	sei();
	
	print("p600firmware\n");
	for(;;)
	{
		p600_update();
	}
}

ISR(TIMER1_COMPA_vect) 
{ 
	p600_slowInterrupt();
}

ISR(TIMER2_COMPA_vect) 
{ 
	p600_fastInterrupt();
}
