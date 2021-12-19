#ifndef UTILS_H
#define	UTILS_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <float.h>
#include <math.h>

#define FORCEINLINE inline __attribute__((always_inline))
#define NOINLINE __attribute__ ((noinline)) 
#define LOWERCODESIZE

#undef MAX
#undef MIN
#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))

uint16_t satAddU16U16(uint16_t a, uint16_t b);
uint16_t satAddU16S32(uint16_t a, int32_t b);
uint16_t satAddU16S16(uint16_t a, int16_t b);
int32_t satAddS16S16(int16_t a, int16_t b);

uint16_t scaleU16U16(uint16_t a, uint16_t b);
int16_t scaleU16S16(uint16_t a, int16_t b);
int16_t scaleProportionalU16S16(uint16_t a, int16_t b);

uint16_t lerp(uint16_t a,uint16_t b,uint8_t x);
uint16_t computeShape(uint32_t phase, const uint16_t lookup[], int8_t interpolate);

uint32_t lfsr(uint32_t v, uint8_t taps);

uint16_t exponentialCourse(uint16_t v, float ratio, float range);

int uint16Compare(const void * a,const void * b); // for qsort

#endif	/* UTILS_H */

