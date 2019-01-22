/*	$OpenBSD: complex.h,v 1.5 2014/03/16 18:38:30 guenther Exp $	*/
/*
 * Copyright (c) 2008 Martynas Venckus <martynas@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __STDC_COMPLEX__
#define	__STDC_COMPLEX__

#include <crtdefs.h>

#define complex _Complex

#define _Complex_I 1.0fi
#define I _Complex_I

/*
 * Macros that can be used to construct complex values.
 *
 * The C99 standard intends x+I*y to be used for this, but x+I*y is
 * currently unusable in general since gcc introduces many overflow,
 * underflow, sign and efficiency bugs by rewriting I*y as
 * (0.0+I)*(y+0.0*I) and laboriously computing the full complex product.
 * In particular, I*Inf is corrupted to NaN+I*Inf, and I*-0 is corrupted
 * to -0.0+I*0.0.
 *
 * In C11, a CMPLX(x,y) macro was added to circumvent this limitation,
 * and gcc 4.7 added a __builtin_complex feature to simplify implementation
 * of CMPLX in libc, so we can take advantage of these features if they
 * are available. Clang simply allows complex values to be constructed
 * using a compound literal.
 *
 * If __builtin_complex is not available, resort to using inline
 * functions instead. These can unfortunately not be used to construct
 * compile-time constants.
 *
 * C99 specifies that complex numbers have the same representation as
 * an array of two elements, where the first element is the real part
 * and the second element is the imaginary part.
 */

#ifdef __clang__
#  define CMPLXF(x, y) ((float complex){x, y})
#  define CMPLX(x, y) ((double complex){x, y})
#  define CMPLXL(x, y) ((long double complex){x, y})
#elif (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 7)) && !defined(__INTEL_COMPILER)
#  define CMPLXF(x,y) __builtin_complex ((float) (x), (float) (y))
#  define CMPLX(x,y) __builtin_complex ((double) (x), (double) (y))
#  define CMPLXL(x,y) __builtin_complex ((long double) (x), (long double) (y))
#else
static inline float complex
CMPLXF(float x, float y)
{
	union {
		float a[2];
		float complex f;
	} z = {{ x, y }};

	return (z.f);
}

static inline double complex
CMPLX(double x, double y)
{
	union {
		double a[2];
		double complex f;
	} z = {{ x, y }};

	return (z.f);
}

static inline long double complex
CMPLXL(long double x, long double y)
{
	union {
		long double a[2];
		long double complex f;
	} z = {{ x, y }};

	return (z.f);
}
#endif

/*
 * Double versions of C99 functions
 */
CRTDECL(double complex, cacos(double complex));
CRTDECL(double complex, casin(double complex));
CRTDECL(double complex, catan(double complex));
CRTDECL(double complex, ccos(double complex));
CRTDECL(double complex, csin(double complex));
CRTDECL(double complex, ctan(double complex));
CRTDECL(double complex, cacosh(double complex));
CRTDECL(double complex, casinh(double complex));
CRTDECL(double complex, catanh(double complex));
CRTDECL(double complex, ccosh(double complex));
CRTDECL(double complex, csinh(double complex));
CRTDECL(double complex, ctanh(double complex));
CRTDECL(double complex, cexp(double complex));
CRTDECL(double complex, clog(double complex));
CRTDECL(double,         cabs(double complex));
CRTDECL(double complex, cpow(double complex, double complex));
CRTDECL(double complex, csqrt(double complex));
CRTDECL(double,         carg(double complex));
CRTDECL(double,         cimag(double complex));
CRTDECL(double complex, conj(double complex));
CRTDECL(double complex, cproj(double complex));
CRTDECL(double,         creal(double complex));

/*
 * Float versions of C99 functions
 */
CRTDECL(float complex,  cacosf(float complex));
CRTDECL(float complex,  casinf(float complex));
CRTDECL(float complex,  catanf(float complex));
CRTDECL(float complex,  ccosf(float complex));
CRTDECL(float complex,  csinf(float complex));
CRTDECL(float complex,  ctanf(float complex));
CRTDECL(float complex,  cacoshf(float complex));
CRTDECL(float complex,  casinhf(float complex));
CRTDECL(float complex,  catanhf(float complex));
CRTDECL(float complex,  ccoshf(float complex));
CRTDECL(float complex,  csinhf(float complex));
CRTDECL(float complex,  ctanhf(float complex));
CRTDECL(float complex,  cexpf(float complex));
CRTDECL(float complex,  clogf(float complex));
CRTDECL(float,          cabsf(float complex));
CRTDECL(float complex,  cpowf(float complex, float complex));
CRTDECL(float complex,  csqrtf(float complex));
CRTDECL(float,          cargf(float complex));
CRTDECL(float,          cimagf(float complex));
CRTDECL(float complex,  conjf(float complex));
CRTDECL(float complex,  cprojf(float complex));
CRTDECL(float,          crealf(float complex));

/*
 * Long double versions of C99 functions
 */
CRTDECL(long double complex,    cacosl(long double complex));
CRTDECL(long double complex,    casinl(long double complex));
CRTDECL(long double complex,    catanl(long double complex));
CRTDECL(long double complex,    ccosl(long double complex));
CRTDECL(long double complex,    csinl(long double complex));
CRTDECL(long double complex,    ctanl(long double complex));
CRTDECL(long double complex,    cacoshl(long double complex));
CRTDECL(long double complex,    casinhl(long double complex));
CRTDECL(long double complex,    catanhl(long double complex));
CRTDECL(long double complex,    ccoshl(long double complex));
CRTDECL(long double complex,    csinhl(long double complex));
CRTDECL(long double complex,    ctanhl(long double complex));
CRTDECL(long double complex,    cexpl(long double complex));
CRTDECL(long double complex,    clogl(long double complex));
CRTDECL(long double,            cabsl(long double complex));
CRTDECL(long double complex,    cpowl(long double complex, long double complex));
CRTDECL(long double complex,    csqrtl(long double complex));
CRTDECL(long double,            cargl(long double complex));
CRTDECL(long double,            cimagl(long double complex));
CRTDECL(long double complex,    conjl(long double complex));
CRTDECL(long double complex,    cprojl(long double complex));
CRTDECL(long double,            creall(long double complex));

#endif /* !__STDC_COMPLEX__ */
