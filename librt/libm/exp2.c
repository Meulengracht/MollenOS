/*
* ====================================================
* Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
*
* Developed at SunSoft, a Sun Microsystems, Inc. business.
* Permission to use, copy, modify, and distribute this
* software is freely granted, provided that this notice
* is preserved.
* ====================================================
*/

#include "private.h"
#include <limits.h>
#include <math.h>

/* Simply a pow, 2 wrapper */
double exp2(double x)
{
	return pow(2.0, x);
}

/* Simply a powf, 2 wrapper */
float exp2f(float x)
{
	return powf(2.0, x);
}

/* Simply a powl, 2 wrapper 
long double exp2l(long double x)
{
	return powl(2.0, x);
} */
