////////////////////////////////////////////////////////////////////////////////
// Useful code not directly synth-related
////////////////////////////////////////////////////////////////////////////////

#include "utils.h"
#include <avr/pgmspace.h>

inline uint16_t satAddU16U16(uint16_t a, uint16_t b)
{
	uint16_t r;

	r =  (b > UINT16_MAX - a) ? UINT16_MAX : b + a;
	
	return r;
}

inline uint16_t satAddU16S32(uint16_t a, int32_t b)
{
	int32_t r;

	r=a;
	r+=b;
	r=MAX(r,0);
	r=MIN(r,UINT16_MAX);
	
	return (uint16_t)r;
}

inline uint16_t satAddU16S16(uint16_t a, int16_t b)
{
	int32_t r;

	r=a;
	r+=b;
	
	r=MAX(r,0);
	r=MIN(r,UINT16_MAX);
	
	return r;
}

inline int32_t satAddS16S16(int16_t a, int16_t b)
{
	int32_t r;

	r=a+b;
	
	//r=MAX(r,INT16_MIN);
	//r=MIN(r,INT16_MAX);
	
	return r;
}

inline uint16_t lerp(uint16_t a,uint16_t b,uint8_t x)
{
	return a+(x*((b-a)>>8));
}

// Table lookup with exact linear interpolation between two entries: a+floor(x*(b-a)/256).
// lerp() above works on (b-a)>>8 only, so the low byte of the step is lost and the result can be too low by up to 254 (for a rising step) or too high (falling).
// Here the step is split in its signed high byte and its low byte (two 8x8 multiplications, no 32 bit multiplication), which gives the exact value.
// It is only used for the sine LFO (the envelopes keep computeShape, which is cheaper and does not need this precision).
uint16_t computeShapeExact(uint32_t phase, const uint16_t lookup[])
{
	uint8_t ai,bi,x;
	uint16_t a,b;
	int16_t d;

	x=phase>>8;
	bi=ai=phase>>16;
	if(ai<UINT8_MAX)
		bi=ai+1;

	a=pgm_read_word(&lookup[ai]);
	b=pgm_read_word(&lookup[bi]);

	d=(int16_t)(b-a); // the difference of two neighbouring table entries always fits a 16 bit signed number
	// x*d = 256*x*hi + x*lo, where d = 256*hi + lo, hi signed (arithmetic shift) and lo = 0..255
	return a+(int16_t)((int16_t)x*(int8_t)(d>>8))+(uint16_t)(((uint16_t)x*(uint8_t)d)>>8);
}

inline uint16_t computeShape(uint32_t phase, const uint16_t lookup[], int8_t interpolate)
{
	uint8_t ai,bi,x;
	uint16_t a,b;
	
	if(interpolate)
	{
		x=phase>>8;
		bi=ai=phase>>16;

		if(ai<UINT8_MAX)
			bi=ai+1;

		// the lookup tables live in flash (PROGMEM), so they must be read with pgm_read_word()
		a=pgm_read_word(&lookup[ai]);
		b=pgm_read_word(&lookup[bi]);

		return lerp(a,b,x);
	}
	else
	{
		// (flash read, see above)
		return pgm_read_word(&lookup[phase>>16]);
	}
}

#ifdef AVR

#include "mult16x16.h"
#include "mult32x16.h"

// forced inline: these tiny multiply helpers cost more in call overhead than in work, and they run in the timer interrupt
inline __attribute__((always_inline)) uint16_t scaleU16U16(uint16_t a, uint16_t b)
{
	uint16_t r;
	
	MultiU16X16toH16(r,a,b);
	
	return r;
}

// forced inline: these tiny multiply helpers cost more in call overhead than in work, and they run in the timer interrupt
inline __attribute__((always_inline)) int16_t scaleU16S16(uint16_t a, int16_t b)
{
	uint16_t r;
	
	MultiSU16X16toH16(r,b,a);
	
	return r;
}

#else

// forced inline: these tiny multiply helpers cost more in call overhead than in work, and they run in the timer interrupt
inline __attribute__((always_inline)) uint16_t scaleU16U16(uint16_t a, uint16_t b)
{
	uint16_t r;
	
	r=((uint32_t)a*b)>>16;
	
	return r;
}

// forced inline: these tiny multiply helpers cost more in call overhead than in work, and they run in the timer interrupt
inline __attribute__((always_inline)) int16_t scaleU16S16(uint16_t a, int16_t b)
{
	uint16_t r;
	
	r=((int32_t)a*b)>>16;
	
	return r;
}


#endif

// forced inline: these tiny multiply helpers cost more in call overhead than in work, and they run in the timer interrupt
inline __attribute__((always_inline)) int16_t scaleProportionalU16S16(uint16_t a, int16_t b)
{
	uint16_t r;
    r=MIN((int32_t)(a*(1.0f + ((float)b)/65536.0f)),UINT16_MAX);
	return r;
}


inline uint32_t lfsr(uint32_t v, uint8_t taps)
{
	uint8_t b24;
	
	while(taps--)
	{
		b24=v>>24;
		
		v<<=1;
		v|=((b24>>7)^(b24>>5)^(b24>>1)^b24)&1;
	}
	
	return v;
}

uint16_t exponentialCourse(uint16_t v, float ratio, float range)
{
	return expf(-(float)v/ratio)*range;
}


int uint16Compare(const void * a,const void * b)
{
	if (*(uint16_t*)a==*(uint16_t*)b)
		return 0;
	else if (*(uint16_t*)a < *(uint16_t*)b)
		return -1;
	else
		return 1;
}
