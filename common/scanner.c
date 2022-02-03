////////////////////////////////////////////////////////////////////////////////
// Keys and buttons scanner
////////////////////////////////////////////////////////////////////////////////

#include "scanner.h"

#define SCANNER_BYTES 16
#define SCANNER_KEYS_START 64
#define SCANNER_DEBOUNCE_TIMEOUT 5

static struct
{
	uint8_t state[SCANNER_BYTES*8];
} scanner;

void scanner_init(void)
{
	memset(&scanner,0,sizeof(scanner));
}

static FORCEINLINE int scanner_state(uint8_t key)
{
	return scanner.state[key]&1;
}

FORCEINLINE int8_t scanner_keyState(uint8_t key)
{
	return scanner_state(key+SCANNER_KEYS_START);
}

FORCEINLINE int8_t scanner_buttonState(p600Button_t button)
{
	return scanner_state(button);
}

static FORCEINLINE void scanner_event(uint8_t key, int8_t pressed)
{
	if (key<SCANNER_KEYS_START)
		synth_buttonEvent(key,pressed);
	else
		synth_keyEvent(key-SCANNER_KEYS_START+SCANNER_BASE_NOTE,pressed,1,HALF_RANGE);
}

int8_t scanner_isKeyDown(uint8_t note)
{
	if (note<SCANNER_BASE_NOTE || note>SCANNER_C5)
		return 0;
	return scanner.state[note-SCANNER_BASE_NOTE+SCANNER_KEYS_START]&1;
}

void scanner_update(int8_t fullScan)
{
	uint8_t i,j,stateIdx;
	uint8_t ps,flag,curState;		

	for(i=fullScan?0:(SCANNER_KEYS_START/8);i<SCANNER_BYTES;++i)
	{
		BLOCK_INT
		{
			io_write(0x08,i);

			CYCLE_WAIT(10);

			ps=io_read(0x0a);
		}

		for(j=0;j<8;++j)
		{
			stateIdx=i*8+j;
			flag=ps&1;
			curState=scanner.state[stateIdx];

			// debounce timeouts
			if(curState&0xfe)
			{
				scanner.state[stateIdx]=curState-2;
			}
			else if(flag ^ (curState&1)) // if state change and not in debounce
			{
				// update state & start debounce timeout
				scanner.state[stateIdx]=flag|(SCANNER_DEBOUNCE_TIMEOUT<<1);
				// do event
				scanner_event(stateIdx,flag);
			}

			ps>>=1;
		}
	}
}
	

