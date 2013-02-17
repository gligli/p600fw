////////////////////////////////////////////////////////////////////////////////
// Keys and buttons scanner
////////////////////////////////////////////////////////////////////////////////

#include "scanner.h"

#define SCANNER_BYTES 16
#define SCANNER_KEYS_START 64

static struct
{
	uint8_t stateBits[SCANNER_BYTES];
} scanner;

void scanner_init(void)
{
	memset(&scanner,0,sizeof(scanner));
}

static inline int scanner_state(uint8_t key)
{
	return (scanner.stateBits[key>>3]&(1<<(key&7)))!=0;
}

int inline scanner_keyState(uint8_t key)
{
	return scanner_state(key+SCANNER_KEYS_START);
}

int inline scanner_buttonState(p600Button_t button)
{
	return scanner_state(button);
}

static inline void scanner_event(uint8_t key, int pressed)
{
	if (key<SCANNER_KEYS_START)
		p600_buttonEvent(key,pressed);
	else
		p600_keyEvent(key-SCANNER_KEYS_START,pressed);
}

void scanner_update(void)
{
	uint8_t i,j;

	for(i=0;i<SCANNER_BYTES;++i)
	{
		uint8_t ps=0;		
		
		HW_ACCESS
		{
			io_write(0x08,i);
			CYCLE_WAIT(8);
			ps=io_read(0x0a);
		}
		
		uint8_t pa=ps^scanner.stateBits[i];
		
		if(pa)
		{
			for(j=0;j<8;++j)
			{
				if(pa & (1<<j))
				{
					scanner_event(i*8+j,(ps&(1<<j))!=0);
				}
			}
		}
		
		scanner.stateBits[i]=ps;
	}
}
	

