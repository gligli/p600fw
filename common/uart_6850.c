////////////////////////////////////////////////////////////////////////////////
// 68b50 MIDI UART access
////////////////////////////////////////////////////////////////////////////////

#include "uart_6850.h"

void uart_init(void)
{
	mem_write(0x6000,0b00000011); // master reset
	MDELAY(1);
	
	mem_write(0x6000,0b10010101); // clock/16 - 8N1 - receive int
	CYCLE_WAIT(8);
	
	mem_read(0xe000); // read status to start the device
	CYCLE_WAIT(8);
}

void NOINLINE uart_send(uint8_t data)
{
		uint8_t status;

	// wait until previous byte is transmitted

	do
	{
		BLOCK_INT
		{
			status=mem_read(0xe000);
		}
		
		CYCLE_WAIT(4);
	}
	while(!(status&0x02));

	// send the new one

	BLOCK_INT
	{
		mem_write(0x6001,data);
		CYCLE_WAIT(4);
	}
}

void uart_update(void)
{
	if(!hardware_getNMIState())
		return;
	
	BLOCK_INT
	{
		uint8_t data,status;

		status=mem_read(0xe000);
		CYCLE_WAIT(4);

		if(!(status&0x80))
		{
#ifdef DEBUG
			print("Error: NMI asserted, no UART IRQ\n");
#endif	
			return;
		}

		if(!(status&0x01))
		{
#ifdef DEBUG
			print("Error: UART IRQ without data\n");
#endif	
			return;
		}

		data=mem_read(0xe001);
		CYCLE_WAIT(4);

		if(status&0x10)
		{
#ifdef DEBUG
			print("Error: UART framing error\n");
#endif	
			uart_init();
			return;
		}

		if(status&0x20)
		{
#ifdef DEBUG
			print("Warning: UART overrun\n");
#endif	
		}

		synth_uartEvent(data);
	}
}

