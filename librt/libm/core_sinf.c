/* k_sinf.c -- float version of k_sin.c
* Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
* Optimized by Bruce D. Evans.
*/

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

#include "private.h"
#include <math.h>

/* |sin(x)/x - s(x)| < 2**-37.5 (~[-4.89e-12, 4.824e-12]). */
static const double
S1 = -1.66666666666666324348e-01, /* 0xBFC55555, 0x55555549 */
S2 = 8.33333333332248946124e-03, /* 0x3F811111, 0x1110F8A6 */
S3 = -1.98412698298579493134e-04, /* 0xBF2A01A0, 0x19C161D5 */
S4 = 2.75573137070700676789e-06; /* 0x3EC71DE3, 0x57B1FE7D */

//__inline float
float
__kernel_sindf(double x)
{
	double r, s, w, z;

	/* Try to optimize for parallel evaluation as in k_tanf.c. */
	z = x*x;
	w = z*z;
	r = S3 + z*S4;
	s = z*x;
	return (float)((x + s*(S1 + z*S2)) + s*w*r);
}
