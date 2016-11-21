#ifndef __INTERNAL_MATH_H
#define __INTERNAL_MATH_H

#ifndef _CRT_ALL_H
#error DO NOT INCLUDE THIS HEADER DIRECTLY
#endif

int     _isinf          (double); /* not exported */
int     _isnanl         (long double); /* not exported */
int     _isinfl         (long double); /* not exported */

static const double twoTo1023  = 8.988465674311579539e307;   // 0x1p1023
static const double twoToM1022 = 2.225073858507201383e-308;  // 0x1p-1022

#if defined(__GNUC__)
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

#define FORCE_EVAL(x) do { \
	if (sizeof(x) == sizeof(float)) {           \
		volatile float __x;                 \
		__x = (x);                          \
	} else if (sizeof(x) == sizeof(double)) {   \
		volatile double __x;                \
		__x = (x);                          \
	} else {                                    \
		volatile long double __x;           \
		__x = (x);                          \
	}                                           \
} while(0)

union IEEEl2bits {
	long double	e;
	struct {
		unsigned int	manl : 32;
		unsigned int	manh : 32;
		unsigned int	exp : 15;
		unsigned int	sign : 1;
		unsigned int	junk : 16;
	} bits;
	struct {
		unsigned long long man;
		unsigned int 	expsign : 16;
		unsigned int	junk : 16;
		unsigned int    morejunk;
	} xbits;
};

/* Definitions provided directly by GCC and Clang. */
#if !(defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__))

#if defined(__GLIBC__)

#include <features.h>
#include <endian.h>
#define __ORDER_LITTLE_ENDIAN__  __LITTLE_ENDIAN
#define __ORDER_BIG_ENDIAN__     __BIG_ENDIAN
#define __BYTE_ORDER__           __BYTE_ORDER

#elif defined(__APPLE__)

#include <machine/endian.h>
#define __ORDER_LITTLE_ENDIAN__  LITTLE_ENDIAN
#define __ORDER_BIG_ENDIAN__     BIG_ENDIAN
#define __BYTE_ORDER__           BYTE_ORDER

#elif defined(_WIN32) || defined(MOLLENOS)

#define __ORDER_LITTLE_ENDIAN__  1234
#define __ORDER_BIG_ENDIAN__     4321
#define __BYTE_ORDER__           __ORDER_LITTLE_ENDIAN__

#endif

#endif /* __BYTE_ORDER__, __ORDER_LITTLE_ENDIAN__ and __ORDER_BIG_ENDIAN__ */

#ifndef __FLOAT_WORD_ORDER__
#define __FLOAT_WORD_ORDER__     __BYTE_ORDER__
#endif

union IEEEf2bits {
	float	f;
	struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
		unsigned int	man : 23;
		unsigned int	exp : 8;
		unsigned int	sign : 1;
#else /* _BIG_ENDIAN */
		unsigned int	sign : 1;
		unsigned int	exp : 8;
		unsigned int	man : 23;
#endif
	} bits;
};

#define	DBL_MANH_SIZE	20
#define	DBL_MANL_SIZE	32

union IEEEd2bits {
	double	d;
	struct {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#if __FLOAT_WORD_ORDER__ == __ORDER_LITTLE_ENDIAN__
		unsigned int	manl : 32;
#endif
		unsigned int	manh : 20;
		unsigned int	exp : 11;
		unsigned int	sign : 1;
#if __FLOAT_WORD_ORDER__ == __ORDER_BIG_ENDIAN__
		unsigned int	manl : 32;
#endif
#else /* _BIG_ENDIAN */
		unsigned int	sign : 1;
		unsigned int	exp : 11;
		unsigned int	manh : 20;
		unsigned int	manl : 32;
#endif
	} bits;
};

#define	LDBL_NBIT	0x80000000
#define	mask_nbit_l(u)	((u).bits.manh &= ~LDBL_NBIT)

#define	LDBL_MANH_SIZE	32
#define	LDBL_MANL_SIZE	32

#define	LDBL_TO_ARRAY32(u, a) do {			\
	(a)[0] = (uint32_t)(u).bits.manl;		\
	(a)[1] = (uint32_t)(u).bits.manh;		\
} while (0)
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
#include <stdint.h>

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

#endif
