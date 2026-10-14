////////////////////////////////////////////////////////////////////////////////
// Keys and buttons scanner
////////////////////////////////////////////////////////////////////////////////

#include "scanner.h"

#define SCANNER_BYTES 16
#define SCANNER_KEYS_START 64
#define SCANNER_DEBOUNCE_TIMEOUT 5
#define SCANNER_BUTTON_DEBOUNCE_TIMEOUT 5 // V2.5 (Teensy): the buttons are scanned at 63Hz again as in V2.34 (the Flat-Labs build scanned them 4x as often and used 20), so V2.34's 5 = ~80 ms

static struct
{
	uint8_t state[SCANNER_BYTES*8];

	// Shadow of 'state' for the fast path in scanner_update (it is always rebuilt from the states
	// of the same byte, so it can never disagree with them):
	uint8_t stable[SCANNER_BYTES];	// bit j = the stable on/off bit (bit 0) of state[i*8+j]
	uint8_t busy[SCANNER_BYTES];	// non zero = at least one of the 8 states of this byte is counting down a debounce timeout
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

#ifdef DIAG
extern void diag_step(uint8_t code);
#endif
static FORCEINLINE void scanner_event(uint8_t key, int8_t pressed)
{
#ifdef DIAG
	{ static uint8_t shown=0; if(shown<4){ ++shown; diag_step(0x8e); } } // "E." (at most 4 times): a key or button event, e.g. a key that reads as pressed at power-on
#endif
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

// Scans one byte (8 inputs) of the matrix: reads it, counts down the debounce timeouts and reports changes.
// (This is the body of the old scan loop, so that the scan can also be done a few bytes at a time.)
static void scanner_scanByte(uint8_t i)
{
	uint8_t j,stateIdx;
	uint8_t ps,flag,curState;		

	uint8_t stable,busy,bit;

	{
		BLOCK_INT
		{
			io_write(CSO0,i);

			CYCLE_WAIT(10);

			ps=io_read(CSO2);
		}

		// Fast path: if no state of this byte is counting down a debounce timeout and every input reads the same
		// as its stable state, the loop below would do nothing at all for these 8 inputs (no timeout to count
		// down, no change to report). That is the case for nearly every byte on nearly every scan, so skip it.
		if(!scanner.busy[i] && ps==scanner.stable[i])
			return;

		stable=0;
		busy=0;
		bit=1;

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
				scanner.state[stateIdx]=flag|(((stateIdx<SCANNER_KEYS_START)?SCANNER_BUTTON_DEBOUNCE_TIMEOUT:SCANNER_DEBOUNCE_TIMEOUT)<<1);
				// do event
				scanner_event(stateIdx,flag);
			}

			// rebuild the shadow of this byte from the state as it is now (read back after the changes above)
			curState=scanner.state[stateIdx];
			if(curState&1)
				stable|=bit;
			if(curState&0xfe)
				busy=1;
			bit<<=1;

			ps>>=1;
		}

		scanner.stable[i]=stable;
		scanner.busy[i]=busy;
	}
}

// Scans the whole matrix (or only the keys) at once. Used at startup; the timer interrupt uses scanner_updateSlice().
void scanner_update(int8_t fullScan)
{
	uint8_t i;

	for(i=fullScan?0:(SCANNER_KEYS_START/8);i<SCANNER_BYTES;++i)
		scanner_scanByte(i);
}

// Scans one eighth of the matrix (2 bytes). Called on every timer tick with a tick counter, so that every byte is
// still scanned once per 8 ticks (250Hz, the same rate as before) and the debounce times are unchanged, but the work
// is spread evenly instead of being done all at once on every 8th tick, which made that one tick much longer than
// the other seven (the worst case tick is what limits how much else the interrupt can do).
// The bytes are scanned in the same order as a full scan, so events also come in the same order.
void scanner_updateSlice(uint8_t tick)
{
	uint8_t i;

	i=(tick&7)*(SCANNER_BYTES/8);

	scanner_scanByte(i);
	scanner_scanByte(i+1);
}
	

