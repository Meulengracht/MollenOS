/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS Standard C - Math Implementation
 */

#ifndef __STDC_MATH__
#define __STDC_MATH__

#include <crtdefs.h>
#include <locale.h>

/*
 * ANSI/POSIX
 */
extern const union __infinity_un {
	unsigned char __uc[8];
	double        __ud;
} __infinity;

extern const union __nan_un {
	unsigned char __uc[sizeof(float)];
	float         __uf;
} __nan;

/* VBS
#if __GNUC_PREREQ__(3, 3) || (defined(__INTEL_COMPILER) && __INTEL_COMPILER >= 800)
#define	__MATH_BUILTIN_CONSTANTS
#endif

#if __GNUC_PREREQ__(3, 0) && !defined(__INTEL_COMPILER)
#define	__MATH_BUILTIN_RELOPS
#endif
*/

//VBS begin
#define __MATH_BUILTIN_CONSTANTS
#define	__MATH_BUILTIN_RELOPS
#ifndef __ISO_C_VISIBLE
#define __ISO_C_VISIBLE 1999
#endif
//VBS end

#ifdef __MATH_BUILTIN_CONSTANTS
#define	HUGE_VAL	__builtin_huge_val()
#else
#define	HUGE_VAL	(__infinity.__ud)
#endif

#define	FP_ILOGB0	(-INT_MAX)
#define	FP_ILOGBNAN	 INT_MAX

#ifdef __MATH_BUILTIN_CONSTANTS
#define	HUGE_VALF	__builtin_huge_valf()
#define	HUGE_VALL	__builtin_huge_vall()
#define	INFINITY	__builtin_inff()
#define	NAN		    __builtin_nanf("")
#else
#define	HUGE_VALF	(float)HUGE_VAL
#define	HUGE_VALL	(long double)HUGE_VAL
#define	INFINITY	HUGE_VALF
#define	NAN		    (__nan.__uf)
#endif /* __MATH_BUILTIN_CONSTANTS */

#define	MATH_ERRNO	1
#define	MATH_ERREXCEPT	2
#define	math_errhandling	MATH_ERREXCEPT

/*
 * XOPEN/SVID
 */
#define	M_E		    2.7182818284590452354	/* e */
#define	M_LOG2E		1.4426950408889634074	/* log 2e */
#define	M_LOG10E	0.43429448190325182765	/* log 10e */
#define	M_LN2		0.69314718055994530942	/* log e2 */
#define	M_LN10		2.30258509299404568402	/* log e10 */
#define	M_PI		3.14159265358979323846	/* pi */
#define	M_PI_2		1.57079632679489661923	/* pi/2 */
#define	M_PI_4		0.78539816339744830962	/* pi/4 */
#define	M_1_PI		0.31830988618379067154	/* 1/pi */
#define	M_2_PI		0.63661977236758134308	/* 2/pi */
#define	M_2_SQRTPI	1.12837916709551257390	/* 2/sqrt(pi) */
#define	M_SQRT2		1.41421356237309504880	/* sqrt(2) */
#define	M_SQRT1_2	0.70710678118654752440	/* 1/sqrt(2) */

#define	MAXFLOAT	((float)3.40282346638528860e+38)
#define	HUGE		MAXFLOAT

#ifdef __LITTLE_ENDIAN
#define __HI(x) *(1+(int*)&x)
#define __LO(x) *(int*)&x
#define __HIp(x) *(1+(int*)x)
#define __LOp(x) *(int*)x
#else
#define __HI(x) *(int*)&x
#define __LO(x) *(1+(int*)&(x))
#define __HIp(x) *(int*)x
#define __LOp(x) *(1+(int*)(x))
#endif

#ifndef _EXCEPTION_DEFINED
#define _EXCEPTION_DEFINED
		struct _exception {
			int    exctyp;
			char*  name;
			double arg1;
			double arg2;
			double retval;
		};
#endif

#ifndef _COMPLEX_DEFINED
#define _COMPLEX_DEFINED
		struct _complex {
			double x,y;
		};
#endif

#ifndef _MATH_TYPES_DEFINED
#define _MATH_TYPES_DEFINED
typedef float float_t;
typedef double double_t;
#endif

CRTDECL(int*, __signgam(void));
#define signgam (*__signgam())

#define _DOMAIN 1
#define _SING 2
#define _OVERFLOW 3
#define _UNDERFLOW 4
#define _TLOSS 5
#define _PLOSS 6

#define EDOM 33
#define ERANGE 34

_CODE_BEGIN
CRTDECL(int, __fpclassifyd(double));
CRTDECL(int, __fpclassifyf(float));
CRTDECL(int, __fpclassifyl(long double));
CRTDECL(int, __isfinitef(float));
CRTDECL(int, __isfinite(double));
CRTDECL(int, __isfinitel(long double));
CRTDECL(int, __isinf(double));
CRTDECL(int, __isinff(float));
CRTDECL(int, __isinfl(long double));
CRTDECL(int, __isnan(double));
CRTDECL(int, __isnanf(float));
CRTDECL(int, __isnanl(long double));
CRTDECL(int, __isnormalf(float));
CRTDECL(int, __isnormal(double));
CRTDECL(int, __isnormall(long double));
CRTDECL(int, __signbit(double));
CRTDECL(int, __signbitf(float));
CRTDECL(int, __signbitl(long double));

/* Integer version of absolute functions 
 * they are defined in stdlib usually as well
 * but keep them here for compat */
#ifndef _CRT_ABS_DEFINED
#define _CRT_ABS_DEFINED
CRTDECL(int,       abs(int));
CRTDECL(long,      labs(long));
CRTDECL(long long, llabs(long long));
#endif

/* 7.12.3.1 */
/* Symbolic constants to classify floating point numbers. */
#define	FP_INFINITE		0x01
#define	FP_NAN			0x02
#define	FP_NORMAL		0x04
#define	FP_SUBNORMAL	0x08
#define	FP_ZERO			0x10

#define	fpclassify(x) \
    ((sizeof (x) == sizeof (float)) ? __fpclassifyf(x) \
    : (sizeof (x) == sizeof (double)) ? __fpclassifyd(x) \
    : __fpclassifyl(x))

/* 7.12.3.2 */
#define	isfinite(x)					\
    ((sizeof (x) == sizeof (float)) ? __isfinitef((float)(x))	\
    : (sizeof (x) == sizeof (double)) ? __isfinite((double)(x))	\
    : __isfinitel(x))


/* 7.12.3.3 */
#define	isinf(x)					\
    ((sizeof (x) == sizeof (float)) ? __isinff((float)(x))	 \
    : (sizeof (x) == sizeof (double)) ? __isinf((double)(x)) \
    : __isinfl(x))

/* 7.12.3.4
 * We don't need to worry about truncation here: A NaN stays a NaN. */
#define	isnan(x)					\
    ((sizeof (x) == sizeof (float)) ? __isnanf((float)(x))   \
    : (sizeof (x) == sizeof (double)) ? __isnan((double)(x)) \
    : __isnanl(x))

/* 7.12.3.5 */
#define	isnormal(x)					\
    ((sizeof (x) == sizeof (float)) ? __isnormalf((float)(x)) \
    : (sizeof (x) == sizeof (double)) ? __isnormal(x)         \
    : __isnormall(x))

/* 7.12.3.6 The signbit macro */
#define	signbit(x)					\
    ((sizeof (x) == sizeof (float)) ? __signbitf(x)	\
    : (sizeof (x) == sizeof (double)) ? __signbit(x)	\
    : __signbitl(x))

/* 7.12.4 Trigonometric functions: Double in C89 */
CRTDECL(double __CRTDECL, cos(double));
CRTDECL(float __CRTDECL, cosf(float));
CRTDECL(long double __CRTDECL, cosl(long double));

CRTDECL(double __CRTDECL, sin(double));
CRTDECL(float __CRTDECL, sinf(float));
CRTDECL(long double __CRTDECL, sinl(long double));

CRTDECL(double __CRTDECL, tan(double));
CRTDECL(float __CRTDECL, tanf(float));
CRTDECL(long double __CRTDECL, tanl(long double));

/* 7.12.5 Hyperbolic functions: Double in C89  */
CRTDECL(double __CRTDECL, cosh(double));
CRTDECL(float __CRTDECL, coshf(float));
CRTDECL(long double __CRTDECL, coshl(long double));

CRTDECL(double __CRTDECL, sinh(double));
CRTDECL(float __CRTDECL, sinhf(float));
CRTDECL(long double __CRTDECL, sinhl(long double));

CRTDECL(double __CRTDECL, tanh(double));
CRTDECL(float __CRTDECL, tanhf(float));
CRTDECL(long double __CRTDECL, tanhl(long double));

/* 7.12.4 Inverse Trigonometric functions: Double in C89 */
CRTDECL(double __CRTDECL, acos(double));
CRTDECL(float __CRTDECL, acosf(float));
CRTDECL(long double __CRTDECL, acosl(long double));

CRTDECL(double __CRTDECL, asin(double));
CRTDECL(float __CRTDECL, asinf(float));
CRTDECL(long double __CRTDECL, asinl(long double));

CRTDECL(double __CRTDECL, atan(double));
CRTDECL(float __CRTDECL, atanf(float));
CRTDECL(long double __CRTDECL, atanl(long double));

CRTDECL(double __CRTDECL, atan2(double, double));
CRTDECL(float __CRTDECL, atan2f(float, float));
CRTDECL(long double __CRTDECL, atan2l(long double, long double));

/* 7.12.5.1  Inverse hyperbolic trig functions  */
CRTDECL(double __CRTDECL, acosh(double));
CRTDECL(float __CRTDECL, acoshf(float));
CRTDECL(long double __CRTDECL, acoshl(long double));

/* 7.12.5.2 */
CRTDECL(double __CRTDECL, asinh(double));
CRTDECL(float __CRTDECL, asinhf(float));
CRTDECL(long double __CRTDECL, asinhl(long double));

/* 7.12.5.3 */
CRTDECL(double __CRTDECL, atanh(double));
CRTDECL(float __CRTDECL, atanhf(float));
CRTDECL(long double __CRTDECL, atanhl(long double));

/* Exponentials and logarithms  */
/* 7.12.6.1 Double in C89 */
CRTDECL(double __CRTDECL, exp(double));
CRTDECL(float __CRTDECL, expf(float));
CRTDECL(long double __CRTDECL, expl(long double));

/* 7.12.6.2 */
CRTDECL(double __CRTDECL, exp2(double));
CRTDECL(float __CRTDECL, exp2f(float));
CRTDECL(long double __CRTDECL, exp2l(long double));

/* 7.12.6.3 The expm1 functions */
CRTDECL(double __CRTDECL, expm1(double));
CRTDECL(float __CRTDECL, expm1f(float));
CRTDECL(long double __CRTDECL, expm1l(long double));

/* 7.12.6.4 Double in C89 */
CRTDECL(double __CRTDECL, frexp(double, int*));
CRTDECL(float __CRTDECL, frexpf(float, int*));
CRTDECL(long double __CRTDECL, frexpl(long double, int*));

/* 7.12.6.5 */
CRTDECL(double __CRTDECL, ldexp(double, int));
CRTDECL(float __CRTDECL, ldexpf(float, int));
CRTDECL(long double __CRTDECL, ldexpl(long double, int));

/* 7.12.6.6 */
CRTDECL(int __CRTDECL, ilogb(double));
CRTDECL(int __CRTDECL, ilogbf(float));
CRTDECL(int __CRTDECL, ilogbl(long double));

/* 7.12.6.7 */
CRTDECL(double __CRTDECL, log(double));
CRTDECL(float __CRTDECL, logf(float));
CRTDECL(long double __CRTDECL, logl(long double));

/* 7.12.6.8 */
CRTDECL(double __CRTDECL, log10(double));
CRTDECL(float __CRTDECL, log10f(float));
CRTDECL(long double __CRTDECL, log10l(long double));

/* 7.12.6.9 */
CRTDECL(double __CRTDECL, log1p(double));
CRTDECL(float __CRTDECL, log1pf(float));
CRTDECL(long double __CRTDECL, log1pl(long double));

/* 7.12.6.10 */
CRTDECL(double __CRTDECL, log2(double));
CRTDECL(float __CRTDECL, log2f(float));
CRTDECL(long double __CRTDECL, log2l(long double));

/* 7.12.6.11 */
CRTDECL(double __CRTDECL, logb(double));
CRTDECL(float __CRTDECL, logbf(float));
CRTDECL(long double __CRTDECL, logbl(long double));

/* 7.12.6.12  Double in C89 */
CRTDECL(double __CRTDECL, modf(double, double*));
CRTDECL(float __CRTDECL, modff(float, float*));
CRTDECL(long double __CRTDECL, modfl(long double, long double*));

/* 7.12.6.13 */
CRTDECL(double __CRTDECL, scalbn(double, int));
CRTDECL(float __CRTDECL, scalbnf(float, int));
CRTDECL(long double __CRTDECL, scalbnl(long double, int));

CRTDECL(double __CRTDECL, scalbln(double, long));
CRTDECL(float __CRTDECL, scalblnf(float, long));
CRTDECL(long double __CRTDECL, scalblnl(long double, long));

/* 7.12.7.1 */
/* Implementations adapted from Cephes versions */
CRTDECL(double __CRTDECL, cbrt(double));
CRTDECL(float __CRTDECL, cbrtf(float));
CRTDECL(long double __CRTDECL, cbrtl(long double));

/* 7.12.7.2 The fabs functions: Double in C89 */
CRTDECL(double __CRTDECL, fabs(double));
CRTDECL(float __CRTDECL, fabsf(float));
CRTDECL(long double __CRTDECL, fabsl(long double));

/* 7.12.7.3  */
CRTDECL(double __CRTDECL, hypot(double, double));
CRTDECL(float __CRTDECL, hypotf(float, float));
CRTDECL(long double __CRTDECL, hypotl(long double, long double));

/* 7.12.7.4 The pow functions. Double in C89 */
CRTDECL(double __CRTDECL, pow(double, double));
CRTDECL(float __CRTDECL, powf(float, float));
CRTDECL(long double __CRTDECL, powl(long double, long double));

/* 7.12.7.5 The sqrt functions. Double in C89. */
CRTDECL(double __CRTDECL, sqrt(double));
CRTDECL(float __CRTDECL, sqrtf(float));
CRTDECL(long double __CRTDECL, sqrtl(long double));

/* 7.12.8.1 The erf functions  */
CRTDECL(double __CRTDECL, erf(double));
CRTDECL(float __CRTDECL, erff(float));
CRTDECL(long double __CRTDECL, erfl(long double));

/* 7.12.8.2 The erfc functions  */
CRTDECL(double __CRTDECL, erfc(double));
CRTDECL(float __CRTDECL, erfcf(float));
CRTDECL(long double __CRTDECL, erfcl(long double));

/* 7.12.8.3 The lgamma functions */
CRTDECL(double __CRTDECL, lgamma(double));
CRTDECL(float __CRTDECL, lgammaf(float));
CRTDECL(long double __CRTDECL, lgammal(long double));

/* Reentrant version of lgamma; passes signgam back by reference as the
 * second argument; user must allocate space for signgam. */
CRTDECL(double __CRTDECL, lgamma_r(double, int *));
CRTDECL(float __CRTDECL, lgammaf_r(float, int*));
CRTDECL(long double __CRTDECL, lgammal_r(long double, int *));

/* 7.12.8.4 The tgamma functions */
CRTDECL(double __CRTDECL, tgamma(double));
CRTDECL(float __CRTDECL, tgammaf(float));
CRTDECL(long double __CRTDECL, tgammal(long double));

/* 7.12.9.1 Double in C89 */
CRTDECL(double __CRTDECL, ceil(double));
CRTDECL(float __CRTDECL, ceilf(float));
CRTDECL(long double __CRTDECL, ceill(long double));

/* 7.12.9.2 Double in C89 */
CRTDECL(double __CRTDECL, floor(double));
CRTDECL(float __CRTDECL, floorf(float));
CRTDECL(long double __CRTDECL, floorl(long double));

/* 7.12.9.3 */
CRTDECL(double __CRTDECL, nearbyint(double));
CRTDECL(float __CRTDECL, nearbyintf(float));
CRTDECL(long double __CRTDECL, nearbyintl(long double));

/* 7.12.9.4 */
/* round, using fpu control word settings */
CRTDECL(double __CRTDECL, rint(double));
CRTDECL(float __CRTDECL, rintf(float));
CRTDECL(long double __CRTDECL, rintl(long double));

/* 7.12.9.5 */
CRTDECL(long __CRTDECL, lrint(double));
CRTDECL(long __CRTDECL, lrintf(float));
CRTDECL(long __CRTDECL, lrintl(long double));

CRTDECL(long long __CRTDECL, llrint(double));
CRTDECL(long long __CRTDECL, llrintf(float));
CRTDECL(long long __CRTDECL, llrintl(long double));

/* 7.12.9.6 */
/* round away from zero, regardless of fpu control word settings */
CRTDECL(double __CRTDECL, round(double));
CRTDECL(float __CRTDECL, roundf(float));
CRTDECL(long double __CRTDECL, roundl(long double));

/* 7.12.9.7  */
CRTDECL(long __CRTDECL, lround(double));
CRTDECL(long __CRTDECL, lroundf(float));
CRTDECL(long __CRTDECL, lroundl(long double));
CRTDECL(long long __CRTDECL, llround(double));
CRTDECL(long long __CRTDECL, llroundf(float));
CRTDECL(long long __CRTDECL, llroundl(long double));

/* 7.12.9.8 */
/* round towards zero, regardless of fpu control word settings */
CRTDECL(double __CRTDECL, trunc(double));
CRTDECL(float __CRTDECL, truncf(float));
CRTDECL(long double __CRTDECL, truncl(long double));

/* 7.12.10.1 Double in C89 */
CRTDECL(double __CRTDECL, fmod(double, double));
CRTDECL(float __CRTDECL, fmodf(float, float));
CRTDECL(long double __CRTDECL, fmodl(long double, long double));

/* 7.12.10.2 */
CRTDECL(double __CRTDECL, remainder(double, double));
CRTDECL(float __CRTDECL, remainderf(float, float));
CRTDECL(long double __CRTDECL, remainderl(long double, long double));

/* 7.12.10.3 */
CRTDECL(double __CRTDECL, remquo(double, double, int*));
CRTDECL(float __CRTDECL, remquof(float, float, int*));
CRTDECL(long double __CRTDECL, remquol(long double, long double, int*));

/* 7.12.11.1 */
CRTDECL(double __CRTDECL, copysign(double, double));
CRTDECL(float __CRTDECL, copysignf(float, float));
CRTDECL(long double __CRTDECL, copysignl(long double, long double));

/* 7.12.11.2 Return a NaN */
CRTDECL(double __CRTDECL, nan(const char* tagp));
CRTDECL(float __CRTDECL, nanf(const char* tagp));
CRTDECL(long double __CRTDECL, nanl(const char* tagp));

#ifndef __STRICT_ANSI__
#define _nan() nan("")
#define _nanf() nanf("")
#define _nanl() nanl("")
#endif

/* 7.12.11.3 */
CRTDECL(double __CRTDECL, nextafter(double, double));
CRTDECL(float __CRTDECL, nextafterf(float, float));
CRTDECL(long double __CRTDECL, nextafterl(long double, long double));

/* 7.12.11.4 The nexttoward functions */
CRTDECL(double __CRTDECL, nexttoward(double, long double));
CRTDECL(float __CRTDECL, nexttowardf(float, long double));
CRTDECL(long double __CRTDECL, nexttowardl(long double, long double));

/* 7.12.12.1 */
/*  x > y ? (x - y) : 0.0  */
CRTDECL(double __CRTDECL, fdim(double, double));
CRTDECL(float __CRTDECL, fdimf(float, float));
CRTDECL(long double __CRTDECL, fdiml(long double, long double));

/* fmax and fmin.
NaN arguments are treated as missing data: if one argument is a NaN
and the other numeric, then these functions choose the numeric
value. */

/* 7.12.12.2 */
CRTDECL(double __CRTDECL, fmax(double, double));
CRTDECL(float __CRTDECL, fmaxf(float, float));
CRTDECL(long double __CRTDECL, fmaxl(long double, long double));

/* 7.12.12.3 */
CRTDECL(double __CRTDECL, fmin(double, double));
CRTDECL(float __CRTDECL, fminf(float, float));
CRTDECL(long double __CRTDECL, fminl(long double, long double));

/* 7.12.13.1 */
/* return x * y + z as a ternary op */
CRTDECL(double __CRTDECL, fma(double, double, double));
CRTDECL(float __CRTDECL, fmaf(float, float, float));
CRTDECL(long double __CRTDECL, fmal(long double, long double, long double));

/* 7.12.13.2 Bessel functions */
CRTDECL(double __CRTDECL, j0(double));
CRTDECL(double __CRTDECL, j1(double));
CRTDECL(double __CRTDECL, jn(int, double));

CRTDECL(float __CRTDECL, j0f(float));
CRTDECL(float __CRTDECL, j1f(float));
CRTDECL(float __CRTDECL, jnf(int, float));

CRTDECL(double __CRTDECL, y0(double));
CRTDECL(double __CRTDECL, y1(double));
CRTDECL(double __CRTDECL, yn(int, double));

CRTDECL(float __CRTDECL, y0f(float));
CRTDECL(float __CRTDECL, y1f(float));
CRTDECL(float __CRTDECL, ynf(int, float));

/* Combined */
CRTDECL(void __CRTDECL, sincos(double, double*, double*));
CRTDECL(void __CRTDECL, sincosf(float, float*, float*));

/* 7.12.14 */
/*
*  With these functions, comparisons involving quiet NaNs set the FP
*  condition code to "unordered".  The IEEE floating-point spec
*  dictates that the result of floating-point comparisons should be
*  false whenever a NaN is involved, except the != op,
*  which always returns true: yes, (NaN != NaN) is true).
*/

#ifdef __GNUC__
#if __GNUC__ >= 3
#define isgreater(x, y) __builtin_isgreater(x, y)
#define isgreaterequal(x, y) __builtin_isgreaterequal(x, y)
#define isless(x, y) __builtin_isless(x, y)
#define islessequal(x, y) __builtin_islessequal(x, y)
#define islessgreater(x, y) __builtin_islessgreater(x, y)
#define isunordered(x, y) __builtin_isunordered(x, y)
#endif
#else
/*  helper  */
#ifndef __CRT__NO_INLINE
__CRT_INLINE int  __CRTDECL
__fp_unordered_compare(long double x, long double y){
	unsigned short retval;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fld [y];
		fucom st(1);
		fnstsw ax;
		mov [retval], ax;
	}
#else
	__asm__ __volatile__("fucom %%st(1);"
		"fnstsw;": "=a" (retval) : "t" (x), "u" (y));
#endif
	return retval;
}
#endif

#define isgreater(x, y) ((__fp_unordered_compare(x, y)  & 0x4500) == 0)
#define isless(x, y) ((__fp_unordered_compare (y, x)  & 0x4500) == 0)
#define isgreaterequal(x, y) ((__fp_unordered_compare (x, y)  & FP_INFINITE) == 0)
#define islessequal(x, y) ((__fp_unordered_compare(y, x)  & FP_INFINITE) == 0)
#define islessgreater(x, y) ((__fp_unordered_compare(x, y)  & FP_SUBNORMAL) == 0)
#define isunordered(x, y) ((__fp_unordered_compare(x, y)  & 0x4500) == 0x4500)

#endif

#ifndef __cplusplus
#define _matherrl _matherr
#endif

#ifndef _CRT_MATHERR_DEFINED
#define _CRT_MATHERR_DEFINED
		int __CRTDECL _matherr(struct _exception *except);
#endif

#ifndef _CRT_ATOF_DEFINED
#define _CRT_ATOF_DEFINED
		 CRTDECL(double __CRTDECL, atof(const char *str));
		 double __CRTDECL _atof_l(const char *str, locale_t locale);
#endif
#ifndef _SIGN_DEFINED
#define _SIGN_DEFINED
		 double __CRTDECL _copysign(double x, double sgn);
		 double __CRTDECL _chgsign(double x);
#endif

#if defined(__i386__) || defined(_M_IX86)
		 int __CRTDECL _set_SSE2_enable(int flag);
#endif

#if defined(__x86_64) || defined(_M_AMD64)
		 float __CRTDECL _copysignf(float x, float sgn);
		 float __CRTDECL _chgsignf(float x);
		 float __CRTDECL _logbf(float x);
		 float __CRTDECL _nextafterf(float x, float y);
		 int __CRTDECL _finitef(float x);
		 int __CRTDECL _isnanf(float x);
		 int __CRTDECL _fpclassf(float x);
#endif

#ifndef	NO_OLDNAMES
#define DOMAIN _DOMAIN
#define SING _SING
#define OVERFLOW _OVERFLOW
#define UNDERFLOW _UNDERFLOW
#define TLOSS _TLOSS
#define PLOSS _PLOSS
#define matherr _matherr
#endif

#ifdef __cplusplus
	}
	extern "C++" {
		template<class _Ty> inline _Ty _Pow_int(_Ty x,int y) {
			unsigned int _N;
			if(y >= 0) _N = (unsigned int)y;
			else _N = (unsigned int)(-y);
			for(_Ty _Z = _Ty(1);;x *= x) {
				if((_N & 1)!=0) _Z *= x;
				if((_N >>= 1)==0) return (y < 0 ? _Ty(1) / _Z : _Z);
			}
		}
	}
#endif

	/*
 * Functions and definitions for controlling the FPU.
 */
#ifndef	__STRICT_ANSI__

/* TODO: These constants are only valid for x86 machines */

/* Control word masks for unMask */
#define	_MCW_EM		0x0008001F	/* Error masks */
#define	_MCW_IC		0x00040000	/* Infinity */
#define	_MCW_RC		0x00000300	/* Rounding */
#define	_MCW_PC		0x00030000	/* Precision */
#define _MCW_DN     0x03000000  /* Denormal */

/* Control word values for unNew (use with related unMask above) */
#define	_EM_INVALID	0x00000010
#define	_EM_DENORMAL	0x00080000
#define	_EM_ZERODIVIDE	0x00000008
#define	_EM_OVERFLOW	0x00000004
#define	_EM_UNDERFLOW	0x00000002
#define	_EM_INEXACT	0x00000001
#define	_IC_AFFINE	0x00040000
#define	_IC_PROJECTIVE	0x00000000
#define	_RC_CHOP	0x00000300
#define	_RC_UP		0x00000200
#define	_RC_DOWN	0x00000100
#define	_RC_NEAR	0x00000000
#define	_PC_24		0x00020000
#define	_PC_53		0x00010000
#define	_PC_64		0x00000000

/* These are also defined in Mingw math.h, needed to work around
   GCC build issues.  */
/* Return values for fpclass. */
#ifndef __MINGW_FPCLASS_DEFINED
#define __MINGW_FPCLASS_DEFINED 1
#define	_FPCLASS_SNAN	0x0001	/* Signaling "Not a Number" */
#define	_FPCLASS_QNAN	0x0002	/* Quiet "Not a Number" */
#define	_FPCLASS_NINF	0x0004	/* Negative Infinity */
#define	_FPCLASS_NN	0x0008	/* Negative Normal */
#define	_FPCLASS_ND	0x0010	/* Negative Denormal */
#define	_FPCLASS_NZ	0x0020	/* Negative Zero */
#define	_FPCLASS_PZ	0x0040	/* Positive Zero */
#define	_FPCLASS_PD	0x0080	/* Positive Denormal */
#define	_FPCLASS_PN	0x0100	/* Positive Normal */
#define	_FPCLASS_PINF	0x0200	/* Positive Infinity */
#endif /* __MINGW_FPCLASS_DEFINED */

/* invalid subconditions (_SW_INVALID also set) */
#define _SW_UNEMULATED		0x0040  /* unemulated instruction */
#define _SW_SQRTNEG		0x0080  /* square root of a neg number */
#define _SW_STACKOVERFLOW	0x0200  /* FP stack overflow */
#define _SW_STACKUNDERFLOW	0x0400  /* FP stack underflow */

/*  Floating point error signals and return codes */
#define _FPE_INVALID		0x81
#define _FPE_DENORMAL		0x82
#define _FPE_ZERODIVIDE		0x83
#define _FPE_OVERFLOW		0x84
#define _FPE_UNDERFLOW		0x85
#define _FPE_INEXACT		0x86
#define _FPE_UNEMULATED		0x87
#define _FPE_SQRTNEG		0x88
#define _FPE_STACKOVERFLOW	0x8a
#define _FPE_STACKUNDERFLOW	0x8b
#define _FPE_EXPLICITGEN	0x8c    /* raise( SIGFPE ); */

#ifndef RC_INVOKED

_CODE_BEGIN
/* Set the FPU control word as cw = (cw & ~unMask) | (unNew & unMask),
 * i.e. change the bits in unMask to have the values they have in unNew,
 * leaving other bits unchanged. */
CRTDECL(unsigned int, _controlfp(unsigned int unNew, unsigned int unMask));
CRTDECL(unsigned int, _control87(unsigned int unNew, unsigned int unMask));
unsigned int __CRTDECL _clearfp (void);	    /* Clear the FPU status word */
unsigned int __CRTDECL _statusfp (void);	/* Report the FPU status word */
#define		_clear87	_clearfp
#define		_status87	_statusfp


/*
   MSVCRT.dll _fpreset initializes the control register to 0x27f,
   the status register to zero and the tag word to 0FFFFh.
   This differs from asm instruction finit/fninit which set control
   word to 0x37f (64 bit mantissa precison rather than 53 bit).
   By default, the mingw version of _fpreset sets fp control as
   per fninit. To use the MSVCRT.dll _fpreset, include CRT_fp8.o when
   building your application.
*/
CRTDECL(void,       _fpreset(void));
#define fpreset()   _fpreset()

/* Global 'variable' for the current floating point error code. */
int * __CRTDECL __fpecode(void);
#define	_fpecode	(*(__fpecode()))

/*
 * IEEE recommended functions.  MS puts them in float.
 * but they really belong in math.h.
 */

double __CRTDECL _chgsign	(double);
double __CRTDECL _copysign (double, double);
double __CRTDECL _logb (double);
double __CRTDECL _nextafter (double, double);
double __CRTDECL _scalb (double, long);

#define _finite(x) isfinite(x)
#define _fpclass(x) fpclassify(x)
//#define _isnan(x) isnan(x)
_CODE_END

#endif //!RC_INVOKED
#endif //!__STRICT_ANSI__
#endif //!__STDC_MATH__
