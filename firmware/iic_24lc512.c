// modified by GliGli for p600pw, original from: https://www.avrfreaks.net/s/topic/a5C3l000000UFymEAG/t075218?page=2

#include "iic_24lc512.h"
#include <avr/io.h>
#include <util/delay.h>
#include "print.h"

typedef struct
{
	uint8_t b0:1;
	uint8_t b1:1;
	uint8_t b2:1;
	uint8_t b3:1;
	uint8_t b4:1;
	uint8_t b5:1;
	uint8_t b6:1;
	uint8_t b7:1;
} bits;

// define all the ports of your microcontroller
#define PORT_A (* (volatile bits *) &PORTA)
#define PIN_A (* (volatile bits *) &PINA)
#define DDR_A (* (volatile bits *) &DDRA)


//******************************************************************************************
// This is based on some example code from David Prentice, thankyou David, I owe you a beer
//
// Notes: 
//  * Have used  "IIC " in place of  "I2C " as CodeVision reserves these words!
//  * This ONLY works with a single Master (the AVR) and single slave (24lc512 in my case)
//  * Compiled with CodeVision AVR
//
// Tested and working (16/09/2009) with MEGA32 and 24LC512 (project: GorF)
// 
#define IICPORT PORT_A
#define IICDDR  DDR_A
#define IICIN   PIN_A
#define WP b6
#define SCL b5
#define SDA b4
#define IICWP  PORT_A.b6
#define IICSCL  PORT_A.b5
#define IICSDA  PORT_A.b4
#define IICSDAOUT   PORT_A.b4
#define IICSDAIN    PIN_A.b4
#define FLOATSDA_HI DDR_A.b4=0; PORT_A.b4=1
#define SINKSDA_LO  DDR_A.b4=1; PORT_A.b4=0
#define OUTPUT  1
#define INPUT   0
#define HI  1
#define LO  0
// These delays give ~50Khz transfer speed
#define QDELAY  _delay_us(2)
#define DELAY   _delay_us(8)
//********************************************
// Init function
//
void iic_init(void)
{
	IICDDR.WP = OUTPUT;     // WP always output
	IICDDR.SCL = OUTPUT;    // SCL always output
	IICDDR.SDA = OUTPUT;    // SDA line changes from input to output 
	IICWP = LO;             // no "write protect"
	IICSCL = LO;            // set both lines to 0
	IICSDA = LO;
	IICSCL = HI;            // Fudge as we don't have a pull up, only valid for SINGLE master and SINGLE slave 
	QDELAY;                 // short delay
	FLOATSDA_HI;            // float SDA (pull up on bus pulls it high)
}
//********************************************
// IIC Start
//
unsigned char iic_start(void)
{
	FLOATSDA_HI;            // float SDA (pull up on bus pulls it high)
	IICSCL = HI;            // Fudge as we don't have a pull up, only valid for SINGLE master and SINGLE slave 
	QDELAY;
	if (IICSDAIN == 0)
		return 0;
	QDELAY;
	SINKSDA_LO;
	QDELAY;
	IICSCL = LO;
	QDELAY;
	return 1;
}
//********************************************
// IIC Restart
//
unsigned char iic_restart(void) 
{ 
	return iic_start(); 
} 
//********************************************
// IIC Stop
//
void iic_stop(void) 
{ 
	SINKSDA_LO;
	QDELAY;
	IICSCL = LO;
	DELAY;
	IICSCL = HI;
	DELAY;
	FLOATSDA_HI;
	QDELAY;
} 
//********************************************
// IIC write
//
unsigned char iic_write(unsigned char ch) 
{ 
	unsigned char cnt, ret; 
	for (cnt = 8; cnt-- != 0; ch <<= 1) { 
		if (ch & 0x80) 
		{
			FLOATSDA_HI;
		} 
		else
		{ 
			SINKSDA_LO;
		} 
		DELAY; 
		IICSCL = HI; 
		DELAY; 
		IICSCL = LO; 
	} 
	FLOATSDA_HI; 
	IICSCL = HI; 
	//Can't do this test as we've just pushed the line high, may need to up the following delay a little
	//while (READ(SCL) == 0);â¤  â¤  â¤ /* are we being held down by slave ? */ 
	QDELAY;
	ret = !IICSDAIN; 
	IICSCL = LO; 
	return ret; 
}
//********************************************
// IIC Read
//
unsigned char iic_read(char more) 
{ 
	unsigned char cnt, ch = 0; 
	
	for (cnt = 8; cnt != 0; --cnt)
    {
		IICSCL = HI;
		DELAY;
		ch <<= 1;
		IICSCL = LO;
		ch |= IICSDAIN;
    }
	if (more)
    {
		SINKSDA_LO;
    }
    else
    {
        FLOATSDA_HI;
    }
	QDELAY;
	IICSCL = HI;
	DELAY;
	IICSCL = LO; 
	QDELAY;
	FLOATSDA_HI;
	QDELAY;         // .kbv leave float hi on bus 
	return ch; 
} 
//********************************************
// IIC send byte (public function)
//
void iic_send_byte(unsigned char addr_msb, unsigned char addr_lsb, unsigned char data)
{
	unsigned char control = 0b10100000;     // '1010' from datasheet
											// '000' = device address
											// '0' = write
	// IIC START                                                     
	while(!iic_start())
	{
		print("IicError 1");        // displays Error 'IIC1'
	}
	// Put Control Byte
	iic_write(control);
	// put Address
	iic_write(addr_msb);
	iic_write(addr_lsb);
	// put data
	iic_write(data);        // in theory we can keep sending bytes here
	// iic stop
	iic_stop();   
	
	_delay_ms(5); // wait write
}
//********************************************
// IIC send page (public function)
//
void iic_send_page(unsigned char addr_msb, unsigned char addr_lsb, const unsigned char *data)
{
	unsigned char control = 0b10100000;     // '1010' from datasheet
											// '000' = device address
											// '0' = write
	unsigned char i;
	// IIC START                                                     
	while(!iic_start())
	{
		print("IicError 1");        // displays Error 'IIC1'
	}
	// Put Control Byte
	iic_write(control);
	// put Address
	iic_write(addr_msb);
	iic_write(addr_lsb);
	// put data
	for(i = 0; i < IIC_PAGE_SIZE; ++i)
		iic_write(*data++);        // in theory we can keep sending bytes here
	// iic stop
	iic_stop();   
	
	_delay_ms(5); // wait write
}
//********************************************
// IIC receive byte (public function)
//
unsigned char iic_receive_byte(unsigned char addr_msb, unsigned char addr_lsb)
{
	unsigned char control = 0b10100000;     // '1010' from datasheet
											// '000' = device address
											// '0' = write
	unsigned char c;
	// IIC START                                                     
	while(!iic_start())
	{
		print("IicError 2");
	}
	// Put Control Byte
	iic_write(control);
	// put Address
	iic_write(addr_msb);
	iic_write(addr_lsb);
	// IIC START                                                     
	while(!iic_start())
	{
		print("IicError 3");
	}
	control = 0b10100001;                   // '1010' from datasheet
											// '000' = device address
											// '1' = read
	// Put Control Byte
	iic_write(control);
	// fetch single byte
	c = iic_read(0);     // in theory we can get a stream of data from the EEPROM
	// iic stop
	iic_stop();
	
	return c;
}
//********************************************
// IIC receive page (public function)
//
void iic_receive_page(unsigned char addr_msb, unsigned char addr_lsb, unsigned char *data)
{
	unsigned char control = 0b10100000;     // '1010' from datasheet
											// '000' = device address
											// '0' = write
	unsigned char i;
	// IIC START                                                     
	while(!iic_start())
	{
		print("IicError 2");
	}
	// Put Control Byte
	iic_write(control);
	// put Address
	iic_write(addr_msb);
	iic_write(addr_lsb);
	// IIC START                                                     
	while(!iic_start())
	{
		print("IicError 3");
	}
	control = 0b10100001;                   // '1010' from datasheet
											// '000' = device address
											// '1' = read
	// Put Control Byte
	iic_write(control);
	// fetch page
	for(i = 0; i < IIC_PAGE_SIZE; ++i)
		*data++ = iic_read(1);        // in theory we can keep sending bytes here
	iic_read(0);
	// iic stop
	iic_stop();                                       
}

void iic_test(void)
{
	phex(iic_receive_byte(0x42,0x38));
	iic_send_byte(0x42, 0x38, 0xa5);
	phex(iic_receive_byte(0x42,0x38));
	iic_send_byte(0x42,0x38, 0x5a);
	phex(iic_receive_byte(0x42,0x38));
	print("\n");

	uint8_t buf[IIC_PAGE_SIZE];

	for(int i = 0; i < IIC_PAGE_SIZE; ++i)
	{
		buf[i] = 0;
		phex(buf[i]);
	}
	print("\n");
	
	iic_send_page(0x13,0x00,buf);
	
	for(int i = 0; i < IIC_PAGE_SIZE; ++i)
	{
		buf[i] = i ^ (IIC_PAGE_SIZE - i);
		phex(buf[i]);
	}
	print("\n");
	
	iic_send_page(0x13,0x00,buf);

	for(int i = 0; i < IIC_PAGE_SIZE; ++i)
	{
		buf[i] = 0;
		phex(buf[i]);
	}
	print("\n");

	iic_receive_page(0x13,0x00,buf);
	

	for(int i = 0; i < IIC_PAGE_SIZE; ++i)
	{
		phex(buf[i]);
	}
	print("\n");
}
	
