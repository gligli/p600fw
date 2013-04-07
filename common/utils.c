////////////////////////////////////////////////////////////////////////////////
// Usefull code not directly synth-related
////////////////////////////////////////////////////////////////////////////////

#include "utils.h"
#include "print.h"

#define INVLOG2 3.321928095f

inline float log2f( float n )  
{  
    return logf( n ) * INVLOG2;  
}

inline double log2( double n )  
{  
    return log( n ) * INVLOG2;  
}

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

inline uint16_t lerp(uint16_t a,uint16_t b,uint8_t x)
{
	return a+(x*((b-a)>>8));
}

inline uint16_t computeShape(uint32_t phase, const uint16_t lookup[])
{
	uint8_t ai,bi,x;
	uint16_t a,b;

	x=phase>>8;
	bi=ai=phase>>16;

	if(ai<UINT8_MAX)
		bi=ai+1;

	a=lookup[ai];
	b=lookup[bi];

	return lerp(a,b,x);
}

#ifdef AVR

#include "mult16x16.h"
#include "mult32x16.h"

inline uint16_t scaleU16U16(uint16_t a, uint16_t b)
{
	uint16_t r;
	
	MultiU16X16toH16(r,a,b);
	
	return r;
}

inline int16_t scaleU16S16(uint16_t a, int16_t b)
{
	uint16_t r;
	
	MultiSU16X16toH16(r,b,a);
	
	return r;
}

#else

inline uint16_t scaleU16U16(uint16_t a, uint16_t b)
{
	uint16_t r;
	
	r=((uint32_t)a*b)>>16;
	
	return r;
}

inline int16_t scaleU16S16(uint16_t a, int16_t b)
{
	uint16_t r;
	
	r=((int32_t)a*b)>>16;
	
	return r;
}

#endif