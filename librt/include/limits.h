#ifndef __LIMITS_INC__
#define __LIMITS_INC__

	/* FLOAT LIMITS */
#include <float.h>

   /* Number of bits for a char object (byte) */
#undef CHAR_BIT
#define CHAR_BIT 8

   /* Minimum value for an object of type signed char */
#undef SCHAR_MIN
#define SCHAR_MIN (-128)

   /* Maximum value for an object of type signed char */
#undef SCHAR_MAX
#define SCHAR_MAX 127

   /* Maximum value for an object of type unsigned char */
#undef UCHAR_MAX
#define UCHAR_MAX 255

   /* Minimum value for an object of type char */
#undef CHAR_MIN
#define CHAR_MIN SCHAR_MIN

   /* Maximum value for an object of type char */
#undef CHAR_MAX
#define CHAR_MAX SCHAR_MAX

   /* Maximum number of bytes in a multibyte character, for any locale */
#undef MB_LEN_MAX
#define MB_LEN_MAX 5

   /* Minimum value for an object of type short int */
#undef SHRT_MIN
#define SHRT_MIN (-32768)

   /* Maximum value for an object of type short int */
#undef SHRT_MAX
#define SHRT_MAX 32767

   /* Maximum value for an object of type unsigned short int */
#undef USHRT_MAX
#define USHRT_MAX 0xFFFF

   /* Minimum value for an object of type int */
#undef INT_MIN
#define INT_MIN (-2147483647 - 1)

   /* Maximum value for an object of type int */
#undef INT_MAX
#define INT_MAX 2147483647

   /* Maximum value for an object of type unsigned int */
#undef UINT_MAX
#define UINT_MAX 0xFFFFFFFF

   /* Minimum value for an object of type long int */
#undef LONG_MIN
#define LONG_MIN (-2147483647L - 1)

   /* Maximum value for an object of type long int */
#undef LONG_MAX
#define LONG_MAX 2147483647L

   /* Maximum value for an object of type unsigned long int */
#undef ULONG_MAX
#define ULONG_MAX 0xFFFFFFFFUL

	/* Maximum value for an object of type unsigned long long int */
#undef LLONG_MAX
#define LLONG_MAX 9223372036854775807ll

	/* Maximum value for an object of type unsigned long long int */
#undef LLONG_MIN
#define LLONG_MIN (-9223372036854775807ll - 1)

	/* Maximum value for an object of type unsigned long long int */
#undef ULLONG_MAX
#define ULLONG_MAX 0xffffffffffffffffull

   /* max value of an "ssize_t" */
#undef SSIZE_MAX
#ifndef SIZE_MAX
#ifdef _WIN64
#define SIZE_MAX _UI64_MAX
#else
#define SIZE_MAX UINT_MAX
#endif
#endif

#define	ARG_MAX		1048320	/* max length of arguments to exec */

#define	LINK_MAX	32767	/* max # of links to a single file */

#ifndef MAX_CANON
#define	MAX_CANON	256	/* max bytes in line for canonical processing */
#endif

#ifndef MAX_INPUT
#define	MAX_INPUT	512	/* max size of a char input buffer */
#endif

#ifndef PATH_MAX
#define	PATH_MAX	1024	/* max # of characters in a path name */
#endif

#define	PIPE_BUF	5120	/* max # bytes atomic in write to a pipe */

#ifndef TMP_MAX
#define	TMP_MAX		17576	/* 26 * 26 * 26 */
#endif

#define	WORD_BIT	32	/* # of bits in a "word" or "int" */
#define	LONG_BIT	32	/* # of bits in a "long" */


#if _INTEGRAL_MAX_BITS >= 8
#define _I8_MIN (-127 - 1)
#define _I8_MAX 127i8
#define _UI8_MAX 0xffu
#endif

#if _INTEGRAL_MAX_BITS >= 16
#define _I16_MIN (-32767 - 1)
#define _I16_MAX 32767i16
#define _UI16_MAX 0xffffu
#endif

#if _INTEGRAL_MAX_BITS >= 32
#define _I32_MIN (-2147483647 - 1)
#define _I32_MAX 2147483647
#define _UI32_MAX 0xffffffffu
#endif

#if defined(__GNUC__) || defined(_GNU_SOURCE)
#undef LONG_LONG_MAX
#define LONG_LONG_MAX 9223372036854775807ll
#undef LONG_LONG_MIN
#define LONG_LONG_MIN (-LONG_LONG_MAX-1)
#undef ULONG_LONG_MAX
#define ULONG_LONG_MAX (2ull * LONG_LONG_MAX + 1ull)
#endif

#if _INTEGRAL_MAX_BITS >= 64
#define _I64_MIN (-9223372036854775807ll - 1)
#define _I64_MAX 9223372036854775807ll
#define _UI64_MAX 0xffffffffffffffffull
#endif


#endif /*__LIMITS_INC__*/
