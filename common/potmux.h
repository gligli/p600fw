#ifndef POTMUX_H
#define	POTMUX_H

#include "p600.h"

// ugly stuff to be able to set as many as 8 p600pot_t as needed at a time

#define VA_NARGS_IMPL(_1,_2,_3,_4,_5,_6,_7,_8,N,...) N
#define VA_NARGS(...) VA_NARGS_IMPL(__VA_ARGS__, 8, 7, 6, 5, 4, 3, 2, 1)

#define __mask(x) ((uint32_t)1<<(x))

#define __add1(a) __mask(a)
#define __add2(a,b) __add1(a) | __mask(b)
#define __add3(a,b,c) __add2(a,b) | __mask(c)
#define __add4(a,b,c,d) __add3(a,b,c) | __mask(d)
#define __add5(a,b,c,d,e) __add4(a,b,c,d) | __mask(e)
#define __add6(a,b,c,d,e,f) __add5(a,b,c,d,e) | __mask(f)
#define __add7(a,b,c,d,e,f,g) __add6(a,b,c,d,e,f) | __mask(g)
#define __add8(a,b,c,d,e,f,g,h) __add7(a,b,c,d,e,f,g) | __mask(h)

#define __potmux_need3(count, ...) potmux_setNeeded(__add ##count (__VA_ARGS__))
#define __potmux_need2(count, ...) __potmux_need3(count,__VA_ARGS__)

#define potmux_need(...) __potmux_need2(VA_NARGS(__VA_ARGS__), __VA_ARGS__)

void potmux_setNeeded(uint32_t needed);
uint16_t potmux_getValue(p600Pot_t pot);

void potmux_init(void);
void potmux_update(void);

#endif	/* POTMUX_H */

