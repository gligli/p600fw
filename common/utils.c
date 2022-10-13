////////////////////////////////////////////////////////////////////////////////
// Useful code not directly synth-related
////////////////////////////////////////////////////////////////////////////////

#include "utils.h"

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

		a=lookup[ai];
		b=lookup[bi];

		return lerp(a,b,x);
	}
	else
	{
		return lookup[phase>>16];
	}
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

inline int16_t scaleProportionalU16S16(uint16_t a, int16_t b)
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
