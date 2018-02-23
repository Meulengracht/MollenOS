/* @(#)s_fabs.c 5.1 93/09/24 */
/*
* ====================================================
* Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
*
* Developed at SunPro, a Sun Microsystems, Inc. business.
* Permission to use, copy, modify, and distribute this
* software is freely granted, provided that this notice
* is preserved.
* ====================================================
*/

/*
* fabs(x) returns the absolute value of x.
*/

#include "private.h"
#include <math.h>

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(fabs)
#endif

double fabs(double x) {
	uint32_t High;
	GET_HIGH_WORD(High, x);
	SET_HIGH_WORD(x, High & 0x7fffffff);
	return x;
}

float fabsf(float x) {
	uint32_t ix;
	GET_FLOAT_WORD(ix, x);
	SET_FLOAT_WORD(x, ix & 0x7fffffff);
	return x;
}

long double fabsl(long double x) {
	union IEEEl2bits u = { 0 };
	u.e = x;
	u.bits.sign = 0;
	return (u.e);
}
