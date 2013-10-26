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

inline int8_t scanner_keyState(uint8_t key)
{
	return scanner_state(key+SCANNER_KEYS_START);
}

inline int8_t scanner_buttonState(p600Button_t button)
{
	return scanner_state(button);
}

static inline void scanner_event(uint8_t key, int8_t pressed)
{
	if (key<SCANNER_KEYS_START)
		synth_buttonEvent(key,pressed);
	else
		synth_keyEvent(key-SCANNER_KEYS_START,pressed);
}

void scanner_update(int8_t fullScan)
{
	uint8_t i,j;
	uint8_t pps,ps,pa;		

	for(i=fullScan?0:(SCANNER_KEYS_START/8);i<SCANNER_BYTES;++i)
	{
		BLOCK_INT
		{
			io_write(0x08,i);

			CYCLE_WAIT(10);

			// debounce (wait for a stable read)
			ps=scanner.stateBits[i];
			do
			{
				pps=ps;
				ps=io_read(0x0a);
				CYCLE_WAIT(10);
			}
			while(ps!=pps);
		}

		pa=ps^scanner.stateBits[i];
		scanner.stateBits[i]=ps;

		if(pa)
			for(j=0;j<8;++j)
			{
				if(pa&1) scanner_event(i*8+j,ps&1);
				pa>>=1;
				ps>>=1;
			}
	}
}
	

