#ifndef UTILS_H
#define	UTILS_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define FORCEINLINE __attribute__((always_inline))
#define NOINLINE __attribute__ ((noinline)) 

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

float log2f( float n );
uint16_t satAddU16U16(uint16_t a, uint16_t b);
uint16_t satAddU16S32(uint16_t a, int32_t b);
uint16_t satAddU16S16(uint16_t a, int16_t b);
uint16_t lerp(uint16_t a,uint16_t b,uint8_t x);
uint16_t computeShape(uint32_t phase, uint16_t lookup[]);

#endif	/* UTILS_H */

