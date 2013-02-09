////////////////////////////////////////////////////////////////////////////////
// Keys and buttons scanner
////////////////////////////////////////////////////////////////////////////////

#include "scanner.h"

static struct
{
	uint8_t stateBits[16];
} scanner;

void scanner_init(void)
{
	memset(&scanner,0,sizeof(scanner));
}

static int scanner_state(uint8_t key)
{
	return (scanner.stateBits[key>>3]&(1<<(key&7)))!=0;
}

int scanner_keyState(uint8_t key)
{
	return scanner_state(key+128);
}

int scanner_buttonState(p600Button_t button)
{
	return scanner_state(button);
}

static void scanner_event(uint8_t key, int pressed)
{
	if (key<128)
		p600_buttonEvent(key,pressed);
	else
		p600_keyEvent(key-128,pressed);
}

void scanner_update(void)
{
	int i,j;
	
	for(i=0;i<sizeof(scanner.stateBits);++i)
	{
		io_write(0x08,i);
		uint8_t ps=io_read(0x0a);
		
		uint8_t pa=ps^scanner.stateBits[i];
		
		if (pa)
		{
			for(j=0;j<8;++j)
			{
				if (pa & (1<<j))
				{
					scanner_event(i*8+j,(ps&(1<<j))!=0);
				}
			}
		}
		
		scanner.stateBits[i]=ps;
	}
}

