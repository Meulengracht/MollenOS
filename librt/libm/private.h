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

#ifndef _MATH_PRIVATE_H_
#define	_MATH_PRIVATE_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <endian.h>
#include <float.h>

/* Includes 
 * - Architecture */
#if defined(i386) || defined(__i386__)
#include "i386/fpmath.h"
#elif defined(amd64) || defined(__amd64__)
#include "amd64/fpmath.h"
#else
/* Dunno */
#endif

#ifndef _LDBL_EQ_DBL
#ifndef LDBL_MANT_DIG
#error "LDBL_MANT_DIG not defined - should be found in float.h"
#elif LDBL_MANT_DIG == DBL_MANT_DIG
#error "double and long double are the same size but LDBL_EQ_DBL is not defined"
#elif LDBL_MANT_DIG == 53
/* This happens when doubles are 32-bits and long doubles are 64-bits.  */
#define	EXT_EXPBITS	    11
#define EXT_FRACHBITS	20
#define	EXT_FRACLBITS	32

#elif LDBL_MANT_DIG == 64
#define	EXT_EXPBITS	    15
#define EXT_FRACHBITS	32
#define	EXT_FRACLBITS	32

#elif LDBL_MANT_DIG == 65
#define	EXT_EXPBITS	    15
#define EXT_FRACHBITS	32
#define	EXT_FRACLBITS	32

#elif LDBL_MANT_DIG == 112
#define	EXT_EXPBITS	    15
#define EXT_FRACHBITS	48
#define	EXT_FRACLBITS	64

#elif LDBL_MANT_DIG == 113
#define	EXT_EXPBITS	    15
#define EXT_FRACHBITS	48
#define	EXT_FRACLBITS	64

#else
#error Unsupported value for LDBL_MANT_DIG
#endif

#define	EXT_EXP_INFNAN	   ((1 << EXT_EXPBITS) - 1)         /* 32767 */
#define	EXT_EXP_BIAS	   ((1 << (EXT_EXPBITS - 1)) - 1)   /* 16383 */
#define	EXT_FRACBITS	   (EXT_FRACLBITS + EXT_FRACHBITS)
#else
#define	EXT_EXPBITS	    11
#define EXT_FRACHBITS	20
#define	EXT_FRACLBITS	32
#endif /* ! _LDBL_EQ_DBL */

/* IEEE Bits for float and double */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
union IEEEf2bits {
	float	f;
	struct {
		unsigned int man  : 23;
		unsigned int exp  : 8;
		unsigned int sign : 1;
	} bits;
};
union IEEEd2bits {
		double	d;
		struct {
#ifdef _DOUBLE_IS_32BITS
			unsigned int manl : EXT_FRACLBITS;
			unsigned int manh : EXT_FRACHBITS;
			unsigned int exp  : EXT_EXPBITS;
			unsigned int sign : 1;
#else
			unsigned long long manl : EXT_FRACLBITS;
			unsigned long long manh : EXT_FRACHBITS;
			unsigned long long exp  : EXT_EXPBITS;
			unsigned long long sign : 1;
#endif
	} bits;
};
#else
union IEEEf2bits {
	float	f;
	struct {
		unsigned int sign : 1;
		unsigned int exp : 8;
		unsigned int man : 23;
	} bits;
};
union IEEEd2bits {
	double	d;
	struct {
#if _DOUBLE_IS_32BITS
		unsigned int sign : 1;
		unsigned int exp  : EXT_EXPBITS;
		unsigned int manh : EXT_FRACHBITS;
		unsigned int manl : EXT_FRACLBITS;
#else
		unsigned long long sign : 1;
		unsigned long long exp  : EXT_EXPBITS;
		unsigned long long manh : EXT_FRACHBITS;
		unsigned long long manl : EXT_FRACLBITS;
#endif
	} bits;
};
#endif

#define	DBL_MANH_SIZE	EXT_FRACHBITS
#define	DBL_MANL_SIZE	EXT_FRACLBITS

/* This is a shortcut for MSVC double intrinsincs
 * that are generated ONLY by msvc, used by __CI* functions */
#if defined(__GNUC__) || defined(__clang__)
#define FPU_DOUBLE(var) double var; \
	__asm__ __volatile__( "fstpl %0;fwait" : "=m" (var) : )
#define FPU_DOUBLES(var1,var2) double var1,var2; \
	__asm__ __volatile__( "fstpl %0;fwait" : "=m" (var2) : ); \
	__asm__ __volatile__( "fstpl %0;fwait" : "=m" (var1) : )
#elif defined(_MSC_VER)
#define FPU_DOUBLE(var) double var; \
	__asm { fstp [var] }; __asm { fwait };
#define FPU_DOUBLES(var1,var2) double var1,var2; \
	__asm { fstp [var1] }; __asm { fwait }; \
	__asm { fstp [var2] }; __asm { fwait };
#endif

/*
 * The original fdlibm code used statements like:
 *	n0 = ((*(int*)&one)>>29)^1;		* index of high word *
 *	ix0 = *(n0+(int*)&x);			* high word of x *
 *	ix1 = *((1-n0)+(int*)&x);		* low word of x *
 * to dig two 32 bit words out of the 64 bit IEEE floating point
 * value.  That is non-ANSI, and, moreover, the gcc instruction
 * scheduler gets it wrong.  We instead use the following macros.
 * Unlike the original code, we determine the endianness at compile
 * time, not at run time; I don't see much benefit to selecting
 * endianness at run time.
 */

/*
 * A union which permits us to convert between a double and two 32 bit
 * ints.
 */
#if __FLOAT_WORD_ORDER__ == __ORDER_BIG_ENDIAN__

typedef union
{
  double value;
  struct
  {
    uint32_t msw;
    uint32_t lsw;
  } parts;
  struct
  {
    uint64_t w;
  } xparts;
} ieee_double_shape_type;

#endif

#if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__

typedef union
{
  double value;
  struct
  {
    uint32_t lsw;
    uint32_t msw;
  } parts;
  struct
  {
    uint64_t w;
  } xparts;
} ieee_double_shape_type;

#endif

/* Get two 32 bit ints from a double.  */
#define EXTRACT_WORDS(ix0,ix1,d)				\
do {								\
  ieee_double_shape_type ew_u;					\
  ew_u.value = (d);						\
  (ix0) = ew_u.parts.msw;					\
  (ix1) = ew_u.parts.lsw;					\
} while (0)

/* Get a 64-bit int from a double. */
#define EXTRACT_WORD64(ix,d)					\
do {								\
  ieee_double_shape_type ew_u;					\
  ew_u.value = (d);						\
  (ix) = ew_u.xparts.w;						\
} while (0)

/* Get the more significant 32 bit int from a double.  */
#define GET_HIGH_WORD(i,d)					\
do {								\
  ieee_double_shape_type gh_u;					\
  gh_u.value = (d);						\
  (i) = gh_u.parts.msw;						\
} while (0)

/* Get the less significant 32 bit int from a double.  */
#define GET_LOW_WORD(i,d)					\
do {								\
  ieee_double_shape_type gl_u;					\
  gl_u.value = (d);						\
  (i) = gl_u.parts.lsw;						\
} while (0)

/* Set a double from two 32 bit ints.  */
#define INSERT_WORDS(d,ix0,ix1)					\
do {								\
  ieee_double_shape_type iw_u;					\
  iw_u.parts.msw = (ix0);					\
  iw_u.parts.lsw = (ix1);					\
  (d) = iw_u.value;						\
} while (0)

/* Set a double from a 64-bit int. */
#define INSERT_WORD64(d,ix)					\
do {								\
  ieee_double_shape_type iw_u;					\
  iw_u.xparts.w = (ix);						\
  (d) = iw_u.value;						\
} while (0)

/* Set the more significant 32 bits of a double from an int.  */
#define SET_HIGH_WORD(d,v)					\
do {								\
  ieee_double_shape_type sh_u;					\
  sh_u.value = (d);						\
  sh_u.parts.msw = (v);						\
  (d) = sh_u.value;						\
} while (0)

/* Set the less significant 32 bits of a double from an int.  */
#define SET_LOW_WORD(d,v)					\
do {								\
  ieee_double_shape_type sl_u;					\
  sl_u.value = (d);						\
  sl_u.parts.lsw = (v);						\
  (d) = sl_u.value;						\
} while (0)

/*
 * A union which permits us to convert between a float and a 32 bit
 * int.
 */

typedef union
{
  float value;
  /* FIXME: Assumes 32 bit int.  */
  unsigned int word;
} ieee_float_shape_type;

/* Get a 32 bit int from a float.  */
#define GET_FLOAT_WORD(i,d)					\
do {								\
  ieee_float_shape_type gf_u;					\
  gf_u.value = (d);						\
  (i) = gf_u.word;						\
} while (0)

/* Set a float from a 32 bit int.  */
#define SET_FLOAT_WORD(d,i)					\
do {								\
  ieee_float_shape_type sf_u;					\
  sf_u.word = (i);						\
  (d) = sf_u.value;						\
} while (0)

/* Get expsign as a 16 bit int from a long double.  */
#define	GET_LDBL_EXPSIGN(i,d)					\
do {								\
  union IEEEl2bits ge_u;					\
  ge_u.e = (d);							\
  (i) = ge_u.xbits.expsign;					\
} while (0)

/* Set expsign of a long double from a 16 bit int.  */
#define	SET_LDBL_EXPSIGN(d,v)					\
do {								\
  union IEEEl2bits se_u;					\
  se_u.e = (d);							\
  se_u.xbits.expsign = (v);					\
  (d) = se_u.e;							\
} while (0)


//VBS
#define	STRICT_ASSIGN(type, lval, rval)	((lval) = (rval))

/* VBS
#ifdef FLT_EVAL_METHOD
// Attempt to get strict C99 semantics for assignment with non-C99 compilers.
#if FLT_EVAL_METHOD == 0 || __GNUC__ == 0
#define	STRICT_ASSIGN(type, lval, rval)	((lval) = (rval))
#else
#define	STRICT_ASSIGN(type, lval, rval) do {	\
	volatile type __lval;			\
						\
	if (sizeof(type) >= sizeof(double))	\
		(lval) = (rval);		\
	else {					\
		__lval = (rval);		\
		(lval) = __lval;		\
	}					\
} while (0)
#endif
#endif
*/

#if __FLOAT_WORD_ORDER__ == __ORDER_BIG_ENDIAN__

typedef union
{
  long double value;
  struct {
    uint32_t mswhi;
    uint32_t mswlo;
    uint32_t lswhi;
    uint32_t lswlo;
  } parts32;
  struct {
    uint64_t msw;
    uint64_t lsw;
  } parts64;
} ieee_quad_shape_type;

#endif

#if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__

typedef union
{
  long double value;
  struct {
    uint32_t lswlo;
    uint32_t lswhi;
    uint32_t mswlo;
    uint32_t mswhi;
  } parts32;
  struct {
    uint64_t lsw;
    uint64_t msw;
  } parts64;
} ieee_quad_shape_type;

#endif

/* Get two 64 bit ints from a long double.  */

#define GET_LDOUBLE_WORDS64(ix0,ix1,d)				\
do {								\
  ieee_quad_shape_type qw_u;					\
  qw_u.value = (d);						\
  (ix0) = qw_u.parts64.msw;					\
  (ix1) = qw_u.parts64.lsw;					\
} while (0)

/* Set a long double from two 64 bit ints.  */

#define SET_LDOUBLE_WORDS64(d,ix0,ix1)				\
do {								\
  ieee_quad_shape_type qw_u;					\
  qw_u.parts64.msw = (ix0);					\
  qw_u.parts64.lsw = (ix1);					\
  (d) = qw_u.value;						\
} while (0)

/* Get the more significant 64 bits of a long double mantissa.  */

#define GET_LDOUBLE_MSW64(v,d)					\
do {								\
  ieee_quad_shape_type sh_u;					\
  sh_u.value = (d);						\
  (v) = sh_u.parts64.msw;					\
} while (0)

/* Set the more significant 64 bits of a long double mantissa from an int.  */

#define SET_LDOUBLE_MSW64(d,v)					\
do {								\
  ieee_quad_shape_type sh_u;					\
  sh_u.value = (d);						\
  sh_u.parts64.msw = (v);					\
  (d) = sh_u.value;						\
} while (0)

/* Get the least significant 64 bits of a long double mantissa.  */

#define GET_LDOUBLE_LSW64(v,d)					\
do {								\
  ieee_quad_shape_type sh_u;					\
  sh_u.value = (d);						\
  (v) = sh_u.parts64.lsw;					\
} while (0)

/* A union which permits us to convert between a long double and
   three 32 bit ints.  */

#if __FLOAT_WORD_ORDER__ == __ORDER_BIG_ENDIAN__
typedef union
{
  long double value;
  struct {
#ifdef __LP64__
    int padh:32;
#endif
    int exp:16;
    int padl:16;
    uint32_t msw;
    uint32_t lsw;
  } parts;
} ieee_extended_shape_type;

#endif

#if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef union
{
  long double value;
  struct {
    uint32_t lsw;
    uint32_t msw;
    int exp:16;
    int padl:16;
#ifdef __LP64__
    int padh:32;
#endif
  } parts;
} ieee_extended_shape_type;
#endif

/* Get three 32 bit ints from a double.  */

#define GET_LDOUBLE_WORDS(se,ix0,ix1,d)				\
do {								\
  ieee_extended_shape_type ew_u;				\
  ew_u.value = (d);						\
  (se) = ew_u.parts.exp;					\
  (ix0) = ew_u.parts.msw;					\
  (ix1) = ew_u.parts.lsw;					\
} while (0)

/* Set a double from two 32 bit ints.  */

#define SET_LDOUBLE_WORDS(d,se,ix0,ix1)				\
do {								\
  ieee_extended_shape_type iw_u;				\
  iw_u.parts.exp = (se);					\
  iw_u.parts.msw = (ix0);					\
  iw_u.parts.lsw = (ix1);					\
  (d) = iw_u.value;						\
} while (0)

/* Get the more significant 32 bits of a long double mantissa.  */

#define GET_LDOUBLE_MSW(v,d)					\
do {								\
  ieee_extended_shape_type sh_u;				\
  sh_u.value = (d);						\
  (v) = sh_u.parts.msw;						\
} while (0)

/* Set the more significant 32 bits of a long double mantissa from an int.  */

#define SET_LDOUBLE_MSW(d,v)					\
do {								\
  ieee_extended_shape_type sh_u;				\
  sh_u.value = (d);						\
  sh_u.parts.msw = (v);						\
  (d) = sh_u.value;						\
} while (0)

/* Get int from the exponent of a long double.  */

#define GET_LDOUBLE_EXP(se,d)					\
do {								\
  ieee_extended_shape_type ge_u;				\
  ge_u.value = (d);						\
  (se) = ge_u.parts.exp;					\
} while (0)

/* Set exponent of a long double from an int.  */

#define SET_LDOUBLE_EXP(d,se)					\
do {								\
  ieee_extended_shape_type se_u;				\
  se_u.value = (d);						\
  se_u.parts.exp = (se);					\
  (d) = se_u.value;						\
} while (0)

/*
 * Functions internal to the math package, yet not static.
 */
double __exp__D(double, double);
struct Double __log__D(double);
long double __p1evll(long double, void *, int);
long double __polevll(long double, void *, int);

/*
 * Common routine to process the arguments to nan(), nanf(), and nanl().
 */
void __scan_nan(uint32_t *__words, int __num_words, const char *__s);

#ifdef _MSC_VER


#else

/* Asm versions of some functions. */
#ifdef __amd64__
static __inline int 
irint(double x)
{
	int n;
	__asm__("cvtsd2si %1,%0" : "=r" (n) : "x" (x));
	return (n);
}
#define	HAVE_EFFICIENT_IRINT
#endif

#ifdef __i386__
static __inline int
irint(double x)
{
	int n;
	__asm__("fistl %0" : "=m" (n) : "t" (x));
	return (n);
}
#define	HAVE_EFFICIENT_IRINT
#endif

#endif /* __GNUCLIKE_ASM */

/* ieee style elementary functions
 *
 * We rename functions here to improve other sources' diffability
 * against fdlibm. */
#define	__ieee754_sqrt	sqrt
#define	__ieee754_acos	acos
#define	__ieee754_acosh	acosh
#define	__ieee754_log	log
#define	__ieee754_log2	log2
#define	__ieee754_atanh	atanh
#define	__ieee754_asin	asin
#define	__ieee754_atan2	atan2
#define	__ieee754_exp	exp
#define	__ieee754_cosh	cosh
#define	__ieee754_fmod	fmod
#define	__ieee754_pow	pow
#define	__ieee754_lgamma lgamma
#define	__ieee754_lgamma_r lgamma_r
#define	__ieee754_log10	log10
#define	__ieee754_sinh	sinh
#define	__ieee754_hypot	hypot
#define	__ieee754_j0	j0
#define	__ieee754_j1	j1
#define	__ieee754_y0	y0
#define	__ieee754_y1	y1
#define	__ieee754_jn	jn
#define	__ieee754_yn	yn
#define	__ieee754_remainder remainder
#define	__ieee754_sqrtf	sqrtf
#define	__ieee754_acosf	acosf
#define	__ieee754_acoshf acoshf
#define	__ieee754_logf	logf
#define	__ieee754_atanhf atanhf
#define	__ieee754_asinf	asinf
#define	__ieee754_atan2f atan2f
#define	__ieee754_expf	expf
#define	__ieee754_coshf	coshf
#define	__ieee754_fmodf	fmodf
#define	__ieee754_powf	powf
#define	__ieee754_lgammaf lgammaf
#define	__ieee754_lgammaf_r lgammaf_r
#define	__ieee754_log10f log10f
#define	__ieee754_log2f log2f
#define	__ieee754_sinhf	sinhf
#define	__ieee754_hypotf hypotf
#define	__ieee754_j0f	j0f
#define	__ieee754_j1f	j1f
#define	__ieee754_y0f	y0f
#define	__ieee754_y1f	y1f
#define	__ieee754_jnf	jnf
#define	__ieee754_ynf	ynf
#define	__ieee754_remainderf remainderf

/* fdlibm kernel function */
int	__kernel_rem_pio2(double*,double*,int,int,int);

/* double precision kernel functions */
#ifdef INLINE_REM_PIO2
__inline
#endif
int	__ieee754_rem_pio2(double,double*);
double	__kernel_sin(double,double,int);
double	__kernel_cos(double,double);
double	__kernel_tan(double,double,int);
double	__ldexp_exp(double,int);
//double complex __ldexp_cexp(double complex,int);

/* float precision kernel functions */
#ifdef INLINE_REM_PIO2F
__inline
#endif
int	__ieee754_rem_pio2f(float,double*);
#ifdef INLINE_KERNEL_SINDF
__inline
#endif
float	__kernel_sindf(double);
#ifdef INLINE_KERNEL_COSDF
__inline
#endif
float	__kernel_cosdf(double);
#ifdef INLINE_KERNEL_TANDF
__inline
#endif
float	__kernel_tandf(double,int);
float	__ldexp_expf(float,int);
//float complex __ldexp_cexpf(float complex,int);

/* long double precision kernel functions */
long double __kernel_sinl(long double, long double, int);
long double __kernel_cosl(long double, long double);
long double __kernel_tanl(long double, long double, int);

#ifdef __KERNEL_LOG
static const double
Lg1 = 6.666666666666735130e-01,  /* 3FE55555 55555593 */
Lg2 = 3.999999999940941908e-01,  /* 3FD99999 9997FA04 */
Lg3 = 2.857142874366239149e-01,  /* 3FD24924 94229359 */
Lg4 = 2.222219843214978396e-01,  /* 3FCC71C5 1D8E78AF */
Lg5 = 1.818357216161805012e-01,  /* 3FC74664 96CB03DE */
Lg6 = 1.531383769920937332e-01,  /* 3FC39A09 D078C69F */
Lg7 = 1.479819860511658591e-01;  /* 3FC2F112 DF3E5244 */

/*
 * We always inline k_log1p(), since doing so produces a
 * substantial performance improvement (~40% on amd64).
 */
static inline double
k_log1p(double f)
{
	double hfsq,s,z,R,w,t1,t2;

 	s = f/(2.0+f);
	z = s*s;
	w = z*z;
	t1= w*(Lg2+w*(Lg4+w*Lg6));
	t2= z*(Lg1+w*(Lg3+w*(Lg5+w*Lg7)));
	R = t2+t1;
	hfsq=0.5*f*f;
	return s*(hfsq+R);
}
#elif defined (__KERNEL_LOGF)

static const float
Lg1 = 6.666666666666735130e-01f,  /* 3FE55555 55555593 */
Lg2 = 3.999999999940941908e-01f,  /* 3FD99999 9997FA04 */
Lg3 = 2.857142874366239149e-01f,  /* 3FD24924 94229359 */
Lg4 = 2.222219843214978396e-01f;  /* 3FCC71C5 1D8E78AF */

static inline float
k_log1pf(float f)
{
	float hfsq, s, z, R, w, t1, t2;

	s = f / ((float)2.0 + f);
	z = s*s;
	w = z*z;
	t1 = w*(Lg2 + w*Lg4);
	t2 = z*(Lg1 + w*Lg3);
	R = t2 + t1;
	hfsq = (float)0.5*f*f;
	return s*(hfsq + R);
}
#endif

#endif /* !_MATH_PRIVATE_H_ */
