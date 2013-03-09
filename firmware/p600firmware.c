#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <string.h>
#include "usb_debug_only.h"
#include "print.h"
#include "teensy_bootloader_hack.h"

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

inline void setAddr(uint16_t addr, uint8_t * b, uint8_t * d, uint8_t * e, int8_t io)
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

inline void setData(uint8_t data, uint8_t * c, uint8_t * d, uint8_t * e)
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

inline void setDataDir(int8_t write)
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

inline void setIdle(void)
{
	PORTF=0xc6;
	PORTC|=0xC0; // port C has data lines, we can't clear them until fully idle
}

inline void hardware_write(int8_t io, uint16_t addr, uint8_t data)
{
	uint8_t b,c,d,e;
	
	// prepare write
	
	setIdle();
	
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

inline uint8_t hardware_read(int8_t io, uint16_t addr)
{
	uint8_t b,c,d,e,v;

	// prepare read

	setIdle();
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
	
	setIdle();
	
	// descramble
	
	v =(c<<7)&0x80;
	v|=(c>>1)&0x03;
	
	v|=(d	 )&0x10;
	v|=(d>>2)&0x08;
	v|=(d>>1)&0x60;
	
	v|=(e<<1)&0x04;
	
	// back to default (write)

	setDataDir(1);

	return v;
}

inline void mem_write(uint16_t address, uint8_t value)
{
	hardware_write(0,address,value);
}

inline void io_write(uint8_t address, uint8_t value)
{
	hardware_write(1,address,value);
}

inline uint8_t mem_read(uint16_t address)
{
	return hardware_read(0,address);
}

inline uint8_t io_read(uint8_t address)
{
	return hardware_read(1,address);
}

void hardware_init(void)
{
	// disable pullups
	
	MCUCR|=(1<<PUD);	
	
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
	
	hardware_read(0,0); // init r/w system
}

// 32Kbytes, just before the bootloader zone

uint32_t storage_addr=0x16000;
uint32_t storage_size=0x8000;


extern uint8_t phaseLookupLo[];

#define xstr(s) str(s)
#define str(s) #s


void NOINLINE BOOTLOADER_SECTION blHack_program_page (uint32_t page, uint8_t *buf)
{
	uint16_t i;
	uint8_t sreg;

	// Disable interrupts.

	sreg = SREG;
	cli();

	eeprom_busy_wait ();

	blHack_page_erase (page);
	boot_spm_busy_wait ();      // Wait until the memory is erased.

	for (i=0; i<SPM_PAGESIZE; i+=2)
	{
		// Set up little-endian word.

		uint16_t w = *buf++;
		w += (*buf++) << 8;

		blHack_page_fill (page + i, w);
	}

	blHack_page_write (page);     // Store buffer in flash page.
	boot_spm_busy_wait();       // Wait until the memory is written.

	// Reenable RWW-section again. We need this if we want to jump back
	// to the application after bootloading.

	blHack_rww_enable ();

	// Re-enable interrupts (if they were ever enabled).

	SREG = sreg;
}

int main(void)
{
	// initialize clock
	
	CPU_PRESCALE(CPU_16MHz);  

	// initialize low level

	hardware_init();

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

	print("p600firmware\n");
#endif

	// initialize synth code

	cli();
	
	p600_init();
	
	sei();
	
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
