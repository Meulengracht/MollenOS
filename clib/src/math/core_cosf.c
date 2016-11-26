/* k_cosf.c -- float version of k_cos.c
* Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
* Debugged and optimized by Bruce D. Evans.
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

/* |cos(x) - c(x)| < 2**-34.1 (~[-5.37e-11, 5.295e-11]). */
static const double
one = 1.0,
C0 = -4.9999999725103100312e-01, /* -0.499999997251031003120 */
C1 = 4.16666666666666019037e-02, /*  0.0416666233237390631894 */
C2 = -1.38888888888741095749e-03, /* 0xBF56C16C, 0x16C15177 */
C3 = 2.48015872894767294178e-05; /* 0x3EFA01A0, 0x19CB1590 */

//__inline float
float
__kernel_cosdf(double x)
{
	double r, w, z;

	/* Try to optimize for parallel evaluation as in k_tanf.c. */
	z = x*x;
	w = z*z;
	r = C2 + z*C3;
	return (float)(((one + z*C0) + w*C1) + (w*z)*r);
}
