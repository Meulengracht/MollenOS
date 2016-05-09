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

		 extern double _HUGE;

#define HUGE_VAL _HUGE
#define _matherrl _matherr

#ifndef _CRT_ABS_DEFINED
#define _CRT_ABS_DEFINED
		 _CRT_EXTERN int __cdecl abs(int x);
		 _CRT_EXTERN long __cdecl labs(long x);
#endif
		double __cdecl acos(double x);
		double __cdecl asin(double x);
		double __cdecl atan(double x);
		double __cdecl atan2(double y, double x);
		_CRT_EXTERN double __cdecl cos(double x);
		#pragma intrinsic(cos)
		double __cdecl cosh(double x);
		#pragma intrinsic(cosh)
		_CRT_EXTERN double __cdecl exp(double x);
		#pragma intrinsic(exp)
		double __cdecl fabs(double x);
		double __cdecl fmod(double x, double y);
		_CRT_EXTERN double __cdecl log(double x);
		#pragma intrinsic(log)
		_CRT_EXTERN double __cdecl log10(double x);
		#pragma intrinsic(log10)
		_CRT_EXTERN double __cdecl pow(double x, double y);
		#pragma intrinsic(pow)
		_CRT_EXTERN double __cdecl sin(double x);
		#pragma intrinsic(sin)
		double __cdecl sinh(double x);
		#pragma intrinsic(sinh)
		_CRT_EXTERN double __cdecl sqrt(double x);
		double __cdecl tan(double x);
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
		 double __cdecl _cabs(struct _complex a);
		 double __cdecl ceil(double x);
		 _CRT_EXTERN double __cdecl floor(double x);
		 _CRT_EXTERN double __cdecl frexp(double x, int *y);
		 double __cdecl _hypot(double x, double y);
		 double __cdecl _j0(double x);
		 double __cdecl _j1(double x);
		 double __cdecl _jn(int x, double y);
		 double __cdecl ldexp(double x, int y);
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
		__CRT_INLINE float fmodf(float x,float y) { return ((float)fmod((double)x,(double)y)); }
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
