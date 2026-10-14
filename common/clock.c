////////////////////////////////////////////////////////////////////////////////
// Common clock for sequencer and arpeggiator
////////////////////////////////////////////////////////////////////////////////

#include "clock.h"

#include "storage.h"

struct
{
	uint16_t counter,speed;
} clock;

// Speed knob curve of the internal clock (one clock step = a 16th note): step length in clock units (500Hz ticks) = BASE*e^(-knob/RATIO),
// where knob counts from the end of the stop zone. It is centered: the middle of the knob is 75 units (150ms, 100BPM as 16th notes) and the whole knob
// spans 8:1 (the slowest step is 212 units = 424ms, the fastest 26 units = 52ms). A larger BASE makes everything slower, a larger RATIO makes the
// knob cover a narrower range of speeds (finer control). BASE = 75*sqrt(range), RATIO = 64511/ln(range), with range = 8.
#define CLOCK_SPEED_BASE 212.0f
#define CLOCK_SPEED_RATIO 31024.0f

inline void clock_setSpeed(uint16_t speed)
{

	if(speed<1024)
		clock.speed=UINT16_MAX;
	else if(settings.syncMode==smInternal)
		clock.speed=exponentialCourse((uint16_t)(speed-1024),CLOCK_SPEED_RATIO,CLOCK_SPEED_BASE); // the live part of the knob (above the stop zone) spans the whole curve
	else
        clock.speed=extClockDividers[(((uint32_t)speed)*16)>>16];
}

inline uint16_t clock_getSpeed(void)
{
	return clock.speed;
}

inline uint16_t clock_getCounter(void)
{
	return clock.counter;
}

inline void clock_reset(void)
{
	clock.counter = INT16_MAX; // not UINT16_MAX, to avoid overflow

}

inline int8_t clock_update(void)
{
	// speed management

	if(clock.speed==UINT16_MAX)
		return 0;

	++clock.counter;

	if(clock.counter<clock.speed)
		return 0;

	clock.counter=0;

	return 1;
}
