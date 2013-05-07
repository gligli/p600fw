#ifndef UTILS_H
#define	UTILS_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <float.h>
#include <math.h>

#define FORCEINLINE inline __attribute__((always_inline))
#define NOINLINE __attribute__ ((noinline)) 

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

float log2f( float n );
double log2( double n );

uint16_t satAddU16U16(uint16_t a, uint16_t b);
uint16_t satAddU16S32(uint16_t a, int32_t b);
uint16_t satAddU16S16(uint16_t a, int16_t b);

uint16_t scaleU16U16(uint16_t a, uint16_t b);
int16_t scaleU16S16(uint16_t a, int16_t b);

uint16_t lerp(uint16_t a,uint16_t b,uint8_t x);
uint16_t computeShape(uint32_t phase, const uint16_t lookup[]);

#endif	/* UTILS_H */

