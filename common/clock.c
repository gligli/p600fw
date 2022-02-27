////////////////////////////////////////////////////////////////////////////////
// Common clock for sequencer and arpeggiator
////////////////////////////////////////////////////////////////////////////////

#include "clock.h"

#include "storage.h"

struct
{
	uint16_t counter,speed;
} clock;

inline void clock_setSpeed(uint16_t speed)
{

	if(speed<1024)
		clock.speed=UINT16_MAX;
	else if(settings.syncMode==smInternal)
		clock.speed=exponentialCourse(speed,22000.0f,500.0f);
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
