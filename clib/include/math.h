/* MollenOS Math Implementation
 * Newlib
 *
 */

#ifndef  _MATH_H_
#define  _MATH_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <crtdefs.h>

/* Useful constants.  */
#define _MOLLENOS 0x100

#define M_E 2.71828182845904523536
#define M_LOG2E 1.44269504088896340736
#define M_LOG10E 0.434294481903251827651
#define M_LN2 0.693147180559945309417
#define M_LN10 2.30258509299404568402
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.785398163397448309616
#define M_1_PI 0.318309886183790671538
#define M_2_PI 0.636619772367581343076
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.707106781186547524401
	
#define __HI(x) *(1+(int*)&x)
#define __LO(x) *(int*)&x
#define __HIp(x) *(1+(int*)x)
#define __LOp(x) *(int*)x

#ifndef _EXCEPTION_DEFINED
#define _EXCEPTION_DEFINED
		struct _exception {
			int type;
			char *name;
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

#define _DOMAIN 1
#define _SING 2
#define _OVERFLOW 3
#define _UNDERFLOW 4
#define _TLOSS 5
#define _PLOSS 6

#define EDOM 33
#define ERANGE 34

_CRT_EXTERN extern double const _HUGE;

#define HUGE_VAL _HUGE

#ifndef _HUGE_ENUF
#define _HUGE_ENUF 1e+300
#endif
#define INFINITY  ((float)(_HUGE_ENUF * _HUGE_ENUF))
#define HUGE_VALD ((double)INFINITY)
#define HUGE_VALF ((float)INFINITY)
#define HUGE_VALL ((long double)INFINITY)
#define NAN       ((float)(INFINITY * 0.0F))

#define _DENORM  (-2)
#define _FINITE  (-1)
#define _INFCODE 1
#define _NANCODE 2

/* 7.12.3.1 */
/*
Return values for fpclassify.
These are based on Intel x87 fpu condition codes
in the high byte of status word and differ from
the return values for MS IEEE 754 extension _fpclass()
*/
#define FP_NAN		0x0100
#define FP_NORMAL	0x0400
#define FP_INFINITE	(FP_NAN | FP_NORMAL)
#define FP_ZERO		0x4000
#define FP_SUBNORMAL	(FP_NORMAL | FP_ZERO)
/* 0x0200 is signbit mask */

/*
We can't inline float or double, because we want to ensure truncation
to semantic type before classification.
(A normal long double value might become subnormal when
converted to double, and zero when converted to float.)
*/

extern int __cdecl __fpclassifyl(long double);
extern int __cdecl __fpclassifyf(float);
extern int __cdecl __fpclassify(double);

#ifndef __CRT__NO_INLINE
__CRT_INLINE int __cdecl __fpclassifyl(long double x) {
	unsigned short sw;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fxam;
		fstsw ax;
		mov word ptr [sw], ax;
	}
#else
	__asm__ __volatile__("fxam; fstsw %%ax;" : "=a" (sw) : "t" (x));
#endif
	return sw & (FP_NAN | FP_NORMAL | FP_ZERO);
}
__CRT_INLINE int __cdecl __fpclassify(double x) {
	unsigned short sw;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fxam;
		fstsw ax;
		mov word ptr[sw], ax;
	}
#else
	__asm__ __volatile__("fxam; fstsw %%ax;" : "=a" (sw) : "t" (x));
#endif
	return sw & (FP_NAN | FP_NORMAL | FP_ZERO);
}
__CRT_INLINE int __cdecl __fpclassifyf(float x) {
	unsigned short sw;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fxam;
		fstsw ax;
		mov word ptr[sw], ax;
	}
#else
	__asm__ __volatile__("fxam; fstsw %%ax;" : "=a" (sw) : "t" (x));
#endif
	return sw & (FP_NAN | FP_NORMAL | FP_ZERO);
}
#endif

#define fpclassify(x) (sizeof (x) == sizeof (float) ? __fpclassifyf (x)	  \
  : sizeof (x) == sizeof (double) ? __fpclassify (x) \
  : __fpclassifyl (x))

/* 7.12.3.2 */
#define isfinite(x) ((fpclassify(x) & FP_NAN) == 0)

/* 7.12.3.3 */
#define isinf(x) (fpclassify(x) == FP_INFINITE)

/* 7.12.3.4 */
/* We don't need to worry about truncation here:
A NaN stays a NaN. */

extern int __cdecl __isnan(double);
extern int __cdecl __isnanf(float);
extern int __cdecl __isnanl(long double);

#ifndef __CRT__NO_INLINE
__CRT_INLINE int __cdecl __isnan(double _x)
{
	unsigned short sw;
#if defined(_MSC_VER)
	_asm {
		fld [_x];
		fxam;
		fstsw ax;
		mov word ptr[sw], ax;
	}
#else
	__asm__ __volatile__("fxam;"
		"fstsw %%ax": "=a" (sw) : "t" (_x));
#endif
	return (sw & (FP_NAN | FP_NORMAL | FP_INFINITE | FP_ZERO | FP_SUBNORMAL))
		== FP_NAN;
}

__CRT_INLINE int __cdecl __isnanf(float _x)
{
	unsigned short sw;
#if defined(_MSC_VER)
	_asm {
		fld [_x];
		fxam;
		fstsw ax;
		mov word ptr[sw], ax;
	}
#else
	__asm__ __volatile__("fxam;"
		"fstsw %%ax": "=a" (sw) : "t" (_x));
#endif
	return (sw & (FP_NAN | FP_NORMAL | FP_INFINITE | FP_ZERO | FP_SUBNORMAL))
		== FP_NAN;
}

__CRT_INLINE int __cdecl __isnanl(long double _x)
{
	unsigned short sw;
#if defined(_MSC_VER)
	_asm {
		fld [_x];
		fxam;
		fstsw ax;
		mov word ptr[sw], ax;
	}
#else
	__asm__ __volatile__("fxam;"
		"fstsw %%ax": "=a" (sw) : "t" (_x));
#endif
	return (sw & (FP_NAN | FP_NORMAL | FP_INFINITE | FP_ZERO | FP_SUBNORMAL))
		== FP_NAN;
}
#endif

#define isnan(x) (sizeof (x) == sizeof (float) ? __isnanf (x)	\
  : sizeof (x) == sizeof (double) ? __isnan (x)	\
  : __isnanl (x))

/* 7.12.3.5 */
#define isnormal(x) (fpclassify(x) == FP_NORMAL)

/* 7.12.3.6 The signbit macro */
extern int __cdecl __signbit(double);
extern int __cdecl __signbitf(float);
extern int __cdecl __signbitl(long double);
#ifndef __CRT__NO_INLINE
__CRT_INLINE int __cdecl __signbit(double x) {
	unsigned short stw;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fxam;
		fstsw ax;
		mov word ptr[stw], ax;
	}
#else
	__asm__ __volatile__("fxam; fstsw %%ax;": "=a" (stw) : "t" (x));
#endif
	return stw & 0x0200;
}

__CRT_INLINE int __cdecl __signbitf(float x) {
	unsigned short stw;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fxam;
		fstsw ax;
		mov word ptr[stw], ax;
	}
#else
	__asm__ __volatile__("fxam; fstsw %%ax;": "=a" (stw) : "t" (x));
#endif
	return stw & 0x0200;
}

__CRT_INLINE int __cdecl __signbitl(long double x) {
	unsigned short stw;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fxam;
		fstsw ax;
		mov word ptr[stw], ax;
	}
#else
	__asm__ __volatile__("fxam; fstsw %%ax;": "=a" (stw) : "t" (x));
#endif
	return stw & 0x0200;
}
#endif

#define signbit(x) (sizeof (x) == sizeof (float) ? __signbitf (x)	\
  : sizeof (x) == sizeof (double) ? __signbit (x)	\
  : __signbitl (x))

/* 7.12.4 Trigonometric functions: Double in C89 */
// Already in math.h

/* 7.12.5 Hyperbolic functions: Double in C89  */
// Already in math.h

/* Inverse hyperbolic trig functions  */
/* 7.12.5.1 */
_CRT_EXTERN double __cdecl acosh(double);
_CRT_EXTERN float __cdecl acoshf(float);
_CRT_EXTERN long double __cdecl acoshl(long double);

/* 7.12.5.2 */
_CRT_EXTERN double __cdecl asinh(double);
_CRT_EXTERN float __cdecl asinhf(float);
_CRT_EXTERN long double __cdecl asinhl(long double);

/* 7.12.5.3 */
_CRT_EXTERN double __cdecl atanh(double);
_CRT_EXTERN float __cdecl atanhf(float);
_CRT_EXTERN long double __cdecl atanhl(long double);

/* Exponentials and logarithms  */
/* 7.12.6.1 Double in C89 */
// exp functions. Already in math.h

/* 7.12.6.2 */
_CRT_EXTERN double __cdecl exp2(double);
_CRT_EXTERN float __cdecl exp2f(float);
_CRT_EXTERN long double __cdecl exp2l(long double);

/* 7.12.6.3 The expm1 functions */
/* TODO: These could be inlined */
_CRT_EXTERN double __cdecl expm1(double);
_CRT_EXTERN float __cdecl expm1f(float);
_CRT_EXTERN long double __cdecl expm1l(long double);

/* 7.12.6.4 Double in C89 */
// frexp functions. Already in math.h

/* 7.12.6.5 */
#define FP_ILOGB0 ((int)0x80000000)
#define FP_ILOGBNAN ((int)0x80000000)
_CRT_EXTERN int __cdecl ilogb(double);
_CRT_EXTERN int __cdecl ilogbf(float);
_CRT_EXTERN int __cdecl ilogbl(long double);

/* 7.12.6.9 */
_CRT_EXTERN double __cdecl log1p(double);
_CRT_EXTERN float __cdecl log1pf(float);
_CRT_EXTERN long double __cdecl log1pl(long double);

/* 7.12.6.10 */
_CRT_EXTERN double __cdecl log2(double);
_CRT_EXTERN float __cdecl log2f(float);
_CRT_EXTERN long double __cdecl log2l(long double);

/* 7.12.6.11 */
_CRT_EXTERN double __cdecl logb(double);
_CRT_EXTERN float __cdecl logbf(float);
_CRT_EXTERN long double __cdecl logbl(long double);

/* Inline versions.  GCC-4.0+ can do a better fast-math optimization
with __builtins. */
#ifndef __CRT__NO_INLINE
__CRT_INLINE double __cdecl logb(double x)
{
	double res = 0.0;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fxtract;
		fstp [res];
	}
#else
	__asm__ __volatile__("fxtract\n\t"
		"fstp	%%st" : "=t" (res) : "0" (x));
#endif
	return res;
}

__CRT_INLINE float __cdecl logbf(float x)
{
	float res = 0.0F;
#if defined(_MSC_VER)
	_asm {
		fld[x];
		fxtract;
		fstp[res];
	}
#else
	__asm__ __volatile__("fxtract\n\t"
		"fstp	%%st" : "=t" (res) : "0" (x));
#endif
	return res;
}

__CRT_INLINE long double __cdecl logbl(long double x)
{
	long double res = 0.0l;
#if defined(_MSC_VER)
	_asm {
		fld[x];
		fxtract;
		fstp[res];
	}
#else
	__asm__ __volatile__("fxtract\n\t"
		"fstp	%%st" : "=t" (res) : "0" (x));
#endif
	return res;
}
#endif /* __CRT__NO_INLINE */

/* 7.12.6.12  Double in C89 */
// modf functions. Already in math.h

/* 7.12.6.13 */
_CRT_EXTERN double __cdecl scalbn(double, int);
_CRT_EXTERN float __cdecl scalbnf(float, int);
_CRT_EXTERN long double __cdecl scalbnl(long double, int);

_CRT_EXTERN double __cdecl scalbln(double, long);
_CRT_EXTERN float __cdecl scalblnf(float, long);
_CRT_EXTERN long double __cdecl scalblnl(long double, long);

/* 7.12.7.1 */
/* Implementations adapted from Cephes versions */
_CRT_EXTERN double __cdecl cbrt(double);
_CRT_EXTERN float __cdecl cbrtf(float);
_CRT_EXTERN long double __cdecl cbrtl(long double);

/* 7.12.7.2 The fabs functions: Double in C89 */
// Already in math.h

/* 7.12.7.3  */
// hypot functions. Already in math.h

/* 7.12.7.4 The pow functions. Double in C89 */
// Already in math.h

/* 7.12.7.5 The sqrt functions. Double in C89. */
// Already in math.h

/* 7.12.8.1 The erf functions  */
_CRT_EXTERN double __cdecl erf(double);
_CRT_EXTERN float __cdecl erff(float);
_CRT_EXTERN long double __cdecl erfl(long double);

/* 7.12.8.2 The erfc functions  */
_CRT_EXTERN double __cdecl erfc(double);
_CRT_EXTERN float __cdecl erfcf(float);
_CRT_EXTERN long double __cdecl erfcl(long double);

/* 7.12.8.3 The lgamma functions */
_CRT_EXTERN double __cdecl lgamma(double);
_CRT_EXTERN float __cdecl lgammaf(float);
_CRT_EXTERN long double __cdecl lgammal(long double);

/* 7.12.8.4 The tgamma functions */
_CRT_EXTERN double __cdecl tgamma(double);
_CRT_EXTERN float __cdecl tgammaf(float);
_CRT_EXTERN long double __cdecl tgammal(long double);

/* 7.12.9.1 Double in C89 */
// ceil functions. Already in math.h

/* 7.12.9.2 Double in C89 */
// floor functions. Already in math.h

/* 7.12.9.3 */
_CRT_EXTERN double __cdecl nearbyint(double);
_CRT_EXTERN float __cdecl nearbyintf(float);
_CRT_EXTERN long double __cdecl nearbyintl(long double);

/* 7.12.9.4 */
/* round, using fpu control word settings */
_CRT_EXTERN double __cdecl rint(double);
_CRT_EXTERN float __cdecl rintf(float);
_CRT_EXTERN long double __cdecl rintl(long double);

/* 7.12.9.5 */
_CRT_EXTERN long __cdecl lrint(double);
_CRT_EXTERN long __cdecl lrintf(float);
_CRT_EXTERN long __cdecl lrintl(long double);

_CRT_EXTERN long long __cdecl llrint(double);
_CRT_EXTERN long long __cdecl llrintf(float);
_CRT_EXTERN long long __cdecl llrintl(long double);

/* Inline versions of above.
GCC 4.0+ can do a better fast-math job with __builtins. */

#ifndef __CRT__NO_INLINE
__CRT_INLINE double __cdecl rint(double x)
{
	double retval = 0.0;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		frndint;
		fstp [retval];
	}
#else
	__asm__ __volatile__("frndint;": "=t" (retval) : "0" (x));
#endif
	return retval;
}

__CRT_INLINE float __cdecl rintf(float x)
{
	float retval = 0.0;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		frndint;
		fstp [retval];
	}
#else
	__asm__ __volatile__("frndint;": "=t" (retval) : "0" (x));
#endif
	return retval;
}

__CRT_INLINE long double __cdecl rintl(long double x)
{
	long double retval = 0.0l;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		frndint;
		fstp [retval];
	}
#else
	__asm__ __volatile__("frndint;": "=t" (retval) : "0" (x));
#endif
	return retval;
}

__CRT_INLINE long __cdecl lrint(double x)
{
	long retval = 0;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fistp [retval];
	}
#else
	__asm__ __volatile__("fistpl %0"  : "=m" (retval) : "t" (x) : "st");
#endif
	return retval;
}

__CRT_INLINE long __cdecl lrintf(float x)
{
	long retval = 0;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fistp [retval];
	}
#else
	__asm__ __volatile__("fistpl %0"  : "=m" (retval) : "t" (x) : "st");
#endif
	return retval;
}

__CRT_INLINE long __cdecl lrintl(long double x)
{
	long retval = 0;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fistp [retval];
	}
#else
	__asm__ __volatile__("fistpl %0"  : "=m" (retval) : "t" (x) : "st");
#endif
}

__CRT_INLINE long long __cdecl llrint(double x)
{
	long long retval = 0ll;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fistp [retval];
	}
#else
	__asm__ __volatile__("fistpll %0"  : "=m" (retval) : "t" (x) : "st");
#endif
	return retval;
}

__CRT_INLINE long long __cdecl llrintf(float x)
{
	long long retval = 0ll;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fistp [retval];
	}
#else
	__asm__ __volatile__("fistpll %0"  : "=m" (retval) : "t" (x) : "st");
#endif
	return retval;
}

__CRT_INLINE long long __cdecl llrintl(long double x)
{
	long long retval = 0ll;
#if defined(_MSC_VER)
	_asm {
		fld [x];
		fistp [retval];
	}
#else
	__asm__ __volatile__("fistpll %0"  : "=m" (retval) : "t" (x) : "st");
#endif
	return retval;
}
#endif /* !__CRT__NO_INLINE */

/* 7.12.9.6 */
/* round away from zero, regardless of fpu control word settings */
extern double __cdecl round(double);
extern float __cdecl roundf(float);
extern long double __cdecl roundl(long double);

/* 7.12.9.7  */
_CRT_EXTERN long __cdecl lround(double);
_CRT_EXTERN long __cdecl lroundf(float);
_CRT_EXTERN long __cdecl lroundl(long double);
_CRT_EXTERN long long __cdecl llround(double);
_CRT_EXTERN long long __cdecl llroundf(float);
_CRT_EXTERN long long __cdecl llroundl(long double);

/* 7.12.9.8 */
/* round towards zero, regardless of fpu control word settings */
_CRT_EXTERN double __cdecl trunc(double);
_CRT_EXTERN float __cdecl truncf(float);
_CRT_EXTERN long double __cdecl truncl(long double);

/* 7.12.10.1 Double in C89 */
// fmod functions. Already in math.h

/* 7.12.10.2 */
_CRT_EXTERN double __cdecl remainder(double, double);
_CRT_EXTERN float __cdecl remainderf(float, float);
_CRT_EXTERN long double __cdecl remainderl(long double, long double);

/* 7.12.10.3 */
_CRT_EXTERN double __cdecl remquo(double, double, int *);
_CRT_EXTERN float __cdecl remquof(float, float, int *);
_CRT_EXTERN long double __cdecl remquol(long double, long double, int *);

/* 7.12.11.1 */
_CRT_EXTERN double __cdecl copysign(double, double); /* in libmoldname.a */
_CRT_EXTERN float __cdecl copysignf(float, float);
_CRT_EXTERN long double __cdecl copysignl(long double, long double);

/* 7.12.11.2 Return a NaN */
_CRT_EXTERN double __cdecl nan(const char *tagp);
_CRT_EXTERN float __cdecl nanf(const char *tagp);
_CRT_EXTERN long double __cdecl nanl(const char *tagp);

#ifndef __STRICT_ANSI__
#define _nan() nan("")
#define _nanf() nanf("")
#define _nanl() nanl("")
#endif

/* 7.12.11.3 */
_CRT_EXTERN double __cdecl nextafter(double, double); /* in libmoldname.a */
_CRT_EXTERN float __cdecl nextafterf(float, float);
_CRT_EXTERN long double __cdecl nextafterl(long double, long double);

/* 7.12.11.4 The nexttoward functions */
_CRT_EXTERN double __cdecl nexttoward(double, long double);
_CRT_EXTERN float __cdecl nexttowardf(float, long double);
_CRT_EXTERN long double __cdecl nexttowardl(long double, long double);

/* 7.12.12.1 */
/*  x > y ? (x - y) : 0.0  */
_CRT_EXTERN double __cdecl fdim(double x, double y);
_CRT_EXTERN float __cdecl fdimf(float x, float y);
_CRT_EXTERN long double __cdecl fdiml(long double x, long double y);

/* fmax and fmin.
NaN arguments are treated as missing data: if one argument is a NaN
and the other numeric, then these functions choose the numeric
value. */

/* 7.12.12.2 */
_CRT_EXTERN double __cdecl fmax(double, double);
_CRT_EXTERN float __cdecl fmaxf(float, float);
_CRT_EXTERN long double __cdecl fmaxl(long double, long double);

/* 7.12.12.3 */
_CRT_EXTERN double __cdecl fmin(double, double);
_CRT_EXTERN float __cdecl fminf(float, float);
_CRT_EXTERN long double __cdecl fminl(long double, long double);

/* 7.12.13.1 */
/* return x * y + z as a ternary op */
_CRT_EXTERN double __cdecl fma(double, double, double);
_CRT_EXTERN float __cdecl fmaf(float, float, float);
_CRT_EXTERN long double __cdecl fmal(long double, long double, long double);

/* 7.12.14 */
/*
*  With these functions, comparisons involving quiet NaNs set the FP
*  condition code to "unordered".  The IEEE floating-point spec
*  dictates that the result of floating-point comparisons should be
*  false whenever a NaN is involved, with the exception of the != op,
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
__CRT_INLINE int  __cdecl
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

#ifndef _CRT_ABS_DEFINED
#define _CRT_ABS_DEFINED
		_CRT_EXTERN int __cdecl abs(int x);
		_CRT_EXTERN long __cdecl labs(long x);
#endif
		_CRT_EXTERN double __cdecl acos(double x);
		_CRT_EXTERN double __cdecl asin(double x);
		_CRT_EXTERN double __cdecl atan(double x);
		_CRT_EXTERN double __cdecl atan2(double y, double x);
		_CRT_EXTERN double __cdecl cos(double x);
		_CRT_EXTERN double __cdecl cosh(double x);
		_CRT_EXTERN double __cdecl exp(double x);
		_CRT_EXTERN double __cdecl exp2(double n);
		_CRT_EXTERN double __cdecl fabs(double x);
		_CRT_EXTERN double __cdecl fmod(double x, double y);
		_CRT_EXTERN double __cdecl log(double x);
		_CRT_EXTERN double __cdecl log10(double x);
		_CRT_EXTERN double __cdecl pow(double x, double y);
		_CRT_EXTERN double __cdecl sin(double x);
		_CRT_EXTERN double __cdecl sinh(double x);
		_CRT_EXTERN double __cdecl sqrt(double x);
		_CRT_EXTERN double __cdecl tan(double x);
		_CRT_EXTERN double __cdecl tanh(double x);
		_CRT_EXTERN double __cdecl copysign(double x, double y);
		_CRT_EXTERN double __cdecl scalbn(double x, int n);
#ifndef _CRT_MATHERR_DEFINED
#define _CRT_MATHERR_DEFINED
		int __cdecl _matherr(struct _exception *except);
#endif

#ifndef _CRT_ATOF_DEFINED
#define _CRT_ATOF_DEFINED
		_CRT_EXTERN double __cdecl atof(const char *str);
		 double __cdecl _atof_l(const char *str ,_locale_t locale);
#endif
#ifndef _SIGN_DEFINED
#define _SIGN_DEFINED
		 double __cdecl _copysign(double x,double sgn);
		 double __cdecl _chgsign(double x);
#endif
		 _CRT_EXTERN double __cdecl _cabs(struct _complex a);
		 _CRT_EXTERN double __cdecl ceil(double x);
		 _CRT_EXTERN double __cdecl floor(double x);
		 _CRT_EXTERN double __cdecl frexp(double x, int *y);
		 _CRT_EXTERN double __cdecl _hypot(double x, double y);
		 double __cdecl _j0(double x);
		 double __cdecl _j1(double x);
		 double __cdecl _jn(int x, double y);
		 _CRT_EXTERN double __cdecl ldexp(double x, int y);
		 _CRT_EXTERN double __cdecl modf(double x, double *y);
		 double __cdecl _y0(double x);
		 double __cdecl _y1(double x);
		 double __cdecl _yn(int x, double y);
		 float __cdecl _hypotf(float x, float y);

#if defined(__i386__) || defined(_M_IX86)
		 int __cdecl _set_SSE2_enable(int flag);
#endif

#if defined(__x86_64) || defined(_M_AMD64)
		 float __cdecl _copysignf(float x, float sgn);
		 float __cdecl _chgsignf(float x);
		 float __cdecl _logbf(float x);
		 float __cdecl _nextafterf(float x,float y);
		 int __cdecl _finitef(float x);
		 int __cdecl _isnanf(float x);
		 int __cdecl _fpclassf(float x);
#endif

#if defined(__ia64__) || defined (_M_IA64)
		 float __cdecl fabsf(float x);
		 float __cdecl ldexpf(float x, int y);
		 long double __cdecl tanl(long double x);
#else
		__CRT_INLINE float __cdecl fabsf(float x) { return ((float)fabs((double)x)); }
		__CRT_INLINE float __cdecl ldexpf(float x, int expn) { return (float)ldexp (x, expn); }
		__CRT_INLINE long double tanl(long double x) { return (tan((double)x)); }
#endif

#if (_MOLLENOS >= 0x100) && \
	(defined(__x86_64) || defined(_M_AMD64) || \
	defined (__ia64__) || defined (_M_IA64))
		 float __cdecl acosf(float x);
		 float __cdecl asinf(float x);
		 float __cdecl atanf(float x);
		 float __cdecl atan2f(float x, float y);
		 float __cdecl ceilf(float x);
		 float __cdecl cosf(float x);
		 float __cdecl coshf(float x);
		 float __cdecl expf(float x);
		 float __cdecl floorf(float x);
		 float __cdecl fmodf(float x, float y);
		 float __cdecl logf(float x);
		 float __cdecl log10f(float x);
		 float __cdecl modff(float x, float *y);
		 float __cdecl powf(float b, float e);
		 float __cdecl sinf(float x);
		 float __cdecl sinhf(float x);
		 float __cdecl sqrtf(float x);
		 float __cdecl tanf(float x);
		 float __cdecl tanhf(float x);
#else
		__CRT_INLINE float acosf(float x) { return ((float)acos((double)x)); }
		__CRT_INLINE float asinf(float x) { return ((float)asin((double)x)); }
		__CRT_INLINE float atanf(float x) { return ((float)atan((double)x)); }
		__CRT_INLINE float atan2f(float x,float y) { return ((float)atan2((double)x,(double)y)); }
		__CRT_INLINE float ceilf(float x) { return ((float)ceil((double)x)); }
		__CRT_INLINE float cosf(float x) { return ((float)cos((double)x)); }
		__CRT_INLINE float coshf(float x) { return ((float)cosh((double)x)); }
		__CRT_INLINE float expf(float x) { return ((float)exp((double)x)); }
		__CRT_INLINE float floorf(float x) { return ((float)floor((double)x)); }
#ifdef MATH_USE_C
		__CRT_INLINE float fmodf(float x,float y) { return ((float)fmod((double)x,(double)y)); }
#endif
		__CRT_INLINE float logf(float x) { return ((float)log((double)x)); }
		__CRT_INLINE float log10f(float x) { return ((float)log10((double)x)); }
		__CRT_INLINE float modff(float x,float *y) {
			double _Di,_Df = modf((double)x,&_Di);
			*y = (float)_Di;
			return ((float)_Df);
		}
		__CRT_INLINE float powf(float x,float y) { return ((float)pow((double)x,(double)y)); }
		__CRT_INLINE float sinf(float x) { return ((float)sin((double)x)); }
		__CRT_INLINE float sinhf(float x) { return ((float)sinh((double)x)); }
		__CRT_INLINE float sqrtf(float x) { return ((float)sqrt((double)x)); }
		__CRT_INLINE float tanf(float x) { return ((float)tan((double)x)); }
		__CRT_INLINE float tanhf(float x) { return ((float)tanh((double)x)); }
#endif

		__CRT_INLINE long double acosl(long double x) { return (acos((double)x)); }
		__CRT_INLINE long double asinl(long double x) { return (asin((double)x)); }
		__CRT_INLINE long double atanl(long double x) { return (atan((double)x)); }
		__CRT_INLINE long double atan2l(long double y, long double x) { return (atan2((double)y, (double)x)); }
		__CRT_INLINE long double ceill(long double x) { return (ceil((double)x)); }
		__CRT_INLINE long double cosl(long double x) { return (cos((double)x)); }
		__CRT_INLINE long double coshl(long double x) { return (cosh((double)x)); }
		__CRT_INLINE long double expl(long double x) { return (exp((double)x)); }
		__CRT_INLINE long double floorl(long double x) { return (floor((double)x)); }
		__CRT_INLINE long double fmodl(long double x, long double y) { return (fmod((double)x, (double)y)); }
		__CRT_INLINE long double frexpl(long double x, int *y) { return (frexp((double)x, y)); }
		__CRT_INLINE long double logl(long double x) { return (log((double)x)); }
		__CRT_INLINE long double log10l(long double x) { return (log10((double)x)); }
		__CRT_INLINE long double powl(long double x, long double y) { return (pow((double)x, (double)y)); }
		__CRT_INLINE long double sinl(long double x) { return (sin((double)x)); }
		__CRT_INLINE long double sinhl(long double x) { return (sinh((double)x)); }
		__CRT_INLINE long double sqrtl(long double x) { return (sqrt((double)x)); }
		__CRT_INLINE long double tanhl(long double x) {return (tanh((double)x)); }
		__CRT_INLINE long double __cdecl fabsl(long double x) { return fabs((double)x); }
		__CRT_INLINE long double _chgsignl(long double _Number) { return _chgsign((double)(_Number)); }
		__CRT_INLINE long double _copysignl(long double _Number, long double _Sign) { return _copysign((double)(_Number),(double)(_Sign)); }
		__CRT_INLINE long double _hypotl(long double x,long double y) { return _hypot((double)(x),(double)(y)); }
		__CRT_INLINE float frexpf(float x, int *y) { return ((float)frexp((double)x,y)); }
		__CRT_INLINE long double ldexpl(long double x, int y) { return ldexp((double)x, y); }
		__CRT_INLINE long double modfl(long double x,long double *y) {
			double _Di,_Df = modf((double)x,&_Di);
			*y = (long double)_Di;
			return (_Df);
		}

#ifndef	NO_OLDNAMES
#define DOMAIN _DOMAIN
#define SING _SING
#define OVERFLOW _OVERFLOW
#define UNDERFLOW _UNDERFLOW
#define TLOSS _TLOSS
#define PLOSS _PLOSS
#define matherr _matherr
#define HUGE _HUGE
		//  double __cdecl cabs(struct _complex x);
#define cabs _cabs
		 double __cdecl hypot(double x,double y);
		 double __cdecl j0(double x);
		 double __cdecl j1(double x);
		 double __cdecl jn(int x,double y);
		 double __cdecl y0(double x);
		 double __cdecl y1(double x);
		 double __cdecl yn(int x,double y);
		__CRT_INLINE float __cdecl hypotf(float x, float y) { return (float) hypot (x, y); }
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

#ifdef	__cplusplus
extern "C" {
#endif

/* Set the FPU control word as cw = (cw & ~unMask) | (unNew & unMask),
 * i.e. change the bits in unMask to have the values they have in unNew,
 * leaving other bits unchanged. */
 unsigned int __cdecl _controlfp (unsigned int unNew, unsigned int unMask);
 unsigned int __cdecl _control87 (unsigned int unNew, unsigned int unMask);


 unsigned int __cdecl _clearfp (void);	/* Clear the FPU status word */
 unsigned int __cdecl _statusfp (void);	/* Report the FPU status word */
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
void __cdecl _fpreset (void);
void __cdecl fpreset (void);

/* Global 'variable' for the current floating point error code. */
 int * __cdecl __fpecode(void);
#define	_fpecode	(*(__fpecode()))

/*
 * IEEE recommended functions.  MS puts them in float.h
 * but they really belong in math.h.
 */

 double __cdecl _chgsign	(double);
 double __cdecl _copysign (double, double);
 double __cdecl _logb (double);
 double __cdecl _nextafter (double, double);
 double __cdecl _scalb (double, long);

 int __cdecl _finite (double);
 int __cdecl _fpclass (double);
 int __cdecl _isnan (double);

#ifdef	__cplusplus
}
#endif

#endif	/* Not RC_INVOKED */

#endif	/* Not __STRICT_ANSI__ */

#endif /* _MATH_H_ */
