#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <util/crc16.h>
#include <string.h>
#include "usb_debug_only.h"
#include "print.h"
#include "iic_24lc512.h"

#include "synth.h"

#define CLOBBERED(reg)	__asm__ __volatile__ ("" : : : reg);

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

static FORCEINLINE void setAddr(uint16_t addr, uint8_t * b, uint8_t * d, uint8_t * e, int8_t io)
{
	uint8_t msb,lsb,vr03,vr05,vr13,vl05; // shifts
	
	lsb=addr;
	
	vr03=lsb>>3;
	vr05=vr03>>2;
	vl05=lsb<<5;

	if(!io)
	{
		msb=addr>>8;
		vr13=msb>>5;
	
		*b|=vr05&0x80;
		*d|=vr13&0x07;
	}
	
	*b|=vr03&0x7f;
	*e|=vl05&0xe0;
}

static FORCEINLINE void setData(uint8_t data, uint8_t * c, uint8_t * d, uint8_t * e)
{
	uint8_t vl1,vl2,vr7,vr1,v00; // shifts
	
	v00=data;
	
	vl1=v00<<1;
	vl2=vl1<<1;
	
	vr1=v00>>1;
	vr7=vr1>>6;
			
	*c|=vr7&0x01;
	*c|=vl1&0x06;
	
	*d|=v00&0x10;
	*d|=vl2&0x20;
	*d|=vl1&0xc0;
	
	*e|=vr1&0x02;
}

static FORCEINLINE void setDataDir(int8_t write)
{
	if(write)
	{
		DDRC=0b11100111;
		DDRD=0b11110111;
		DDRE=0b11100010;
	}		
	else
	{
		DDRC=0b11100000;
		DDRD=0b00000111;
		DDRE=0b11100000;
	}
}

static FORCEINLINE void setIdle(int8_t fromWrite)
{
	PORTF=0xc6;
	
	if(fromWrite)
		PORTC|=0x40;
	else
		PORTC|=0x80;
}

static FORCEINLINE void hardware_write(int8_t io, uint16_t addr, uint8_t data)
{
	uint8_t b,c,d,e;
	
	// prepare write
	
	setIdle(1);
	
	b=0x00;
	c=(io)?0x40:0x80;
	d=0x00;
	e=0x00;
	
	// address
	
	setAddr(addr,&b,&d,&e,io);
	
	// data
	
	setData(data,&c,&d,&e);
	
	// output it
	
	PORTB=b;
	PORTC=c;
	PORTD=d;
	PORTE=e;

	PORTF=0x87;
}

static FORCEINLINE uint8_t hardware_read(int8_t io, uint16_t addr)
{
	uint8_t b,c,d,e,v;

	// prepare read

	setIdle(1);
	setDataDir(0);

	b=0x00;
	c=(io)?0x40:0x80;
	d=0x00;
	e=0x00;
	
	// address
	
	setAddr(addr,&b,&d,&e,io);

	// output it
	
	PORTB=b;
	PORTC=c;
	PORTD=d;
	PORTE=e;

	PORTF=0x47;

	// let hardware process it
	
	CYCLE_WAIT(2);
	
	// read data
	
	c=PINC;
	d=PIND;
	e=PINE;
	
	// back to idle
	
	setIdle(0);
	
	// descramble
	
	v =(c<<7)&0x80;
	v|=(c>>1)&0x03;
	
	v|=(d   )&0x10;
	v|=(d>>2)&0x08;
	v|=(d>>1)&0x60;
	
	v|=(e<<1)&0x04;
	
	// back to default (write)

	setDataDir(1);

	return v;
}

FORCEINLINE void mem_write(uint16_t address, uint8_t value)
{
	hardware_write(0,address,value);
}

FORCEINLINE void io_write(uint8_t address, uint8_t value)
{
	hardware_write(1,address,value);
}

FORCEINLINE uint8_t mem_read(uint16_t address)
{
	return hardware_read(0,address);
}

FORCEINLINE uint8_t io_read(uint8_t address)
{
	return hardware_read(1,address);
}

FORCEINLINE int8_t hardware_getNMIState(void)
{
	return !(PINC&0x10);
}

static FORCEINLINE void hardware_init(int8_t ints)
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
	
	if(ints)
	{
		// prepare a 2Khz interrupt

		OCR0A=124;
		TCCR0A|=(1<<WGM01); //Timer 0 Clear-Timer on Compare (CTC) 
		TCCR0B|=(1<<CS01) | (1<<CS00);  //Timer 0 prescaler = 64
		TIMSK0|=(1<<OCIE0A); //Enable overflow interrupt for Timer0

#ifdef UART_USE_HW_INTERRUPT	
		EIMSK|=(1<<INT4); // enable INT4
#else
		// prepare a 5Khz interrupt

		OCR2A=49;
		TCCR2A|=(1<<WGM21); //Timer 2 Clear-Timer on Compare (CTC) 
		TCCR2B|=(1<<CS22);  //Timer 2 prescaler = 64
		TIMSK2|=(1<<OCIE2A); //Enable overflow interrupt for Timer2
#endif	
	}
	
	hardware_read(0,0); // init r/w system
	
	
	iic_init();
}

void storage_write(uint32_t pageIdx, uint8_t *buf)
{
	if(pageIdx<(STORAGE_SIZE/STORAGE_PAGE_SIZE))
	{
		iic_send_page(pageIdx, 0, &buf[0]);
		iic_send_page(pageIdx, IIC_PAGE_SIZE, &buf[IIC_PAGE_SIZE]);
	}
}

void storage_read(uint32_t pageIdx, uint8_t *buf)
{
	if(pageIdx<(STORAGE_SIZE/STORAGE_PAGE_SIZE))
	{
		iic_receive_page(pageIdx, 0, &buf[0]);
		iic_receive_page(pageIdx, IIC_PAGE_SIZE, &buf[IIC_PAGE_SIZE]);
	}
}

int main(void)
{
	CPU_PRESCALE(CPU_16MHz);  

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
	_delay_ms(1000);

	print("p600firmware\n");
#endif
	
	// no interrupts while we init
	
	cli();
	
	// initialize low level

	hardware_init(1);

	// initialize synth code

	synth_init();
	
	// all inited, enable ints and do periodical updates
	
	sei();
	
	for(;;)
	{
		synth_update();
	}
}

ISR(TIMER0_COMPA_vect)
{
	// use nested interrupts, because we must still handle synth_uartInterrupt
	// we need to ensure we won't try to recursively handle another synth_timerInterrupt!
	
	TIMSK0&=~(1<<OCIE0A); //Disable overflow interrupt for Timer0
	TIFR0|=7; // Clear any pending interrupt
	sei();

	synth_timerInterrupt();

	cli();
	TIMSK0|=(1<<OCIE0A); //Re-enable overflow interrupt for Timer0
}

#ifdef UART_USE_HW_INTERRUPT
ISR(INT4_vect) 
#else
ISR(TIMER2_COMPA_vect) 
#endif
{ 
	synth_uartInterrupt();
}


ISR(USB_GEN_vect)
{
#ifdef DEBUG
	usb_gen_int();
#endif
}

ISR(USB_COM_vect)
{
#ifdef DEBUG
	usb_com_int();
#endif
}
