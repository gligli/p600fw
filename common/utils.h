#ifndef UTILS_H
#define	UTILS_H

#include <string.h>
#include <stdint.h>
#include <math.h>

#define FORCEINLINE __attribute__((always_inline))
#define NOINLINE __attribute__ ((noinline)) 

#define MAX(a,b) (((a)>(b))?(a):(b))
#define MIN(a,b) (((a)<(b))?(a):(b))
#define SADD16(a, b)  (uint16_t)( ((int32_t)(a)+(int32_t)(b)) > 0xffff ? 0xffff : ((a)+(b)))

float log2f( float n );

#endif	/* UTILS_H */

