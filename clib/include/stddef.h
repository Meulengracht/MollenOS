/*


*/

#ifndef __STDDEF_INC__
#define __STDDEF_INC__

//Includes
#include <stdint.h>
#include <crtdefs.h>
#include <vadefs.h>

#define __STDC_VERSION__ 199901L

//ptrdiff
typedef signed int ptrdiff_t;


#ifdef NULL
#  undef NULL
#endif

/* A null pointer constant.  */
#ifndef NULL
#ifdef __cplusplus
#define NULL 0
#else
#define NULL ((void*)0)
#endif
#endif

#ifndef offsetof
/* Offset of member MEMBER in a struct of type TYPE. */
#if defined(__GNUC__)
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((size_t)&(((TYPE *)0)->MEMBER))
#endif

#endif /* !offsetof */

//Structures
typedef struct Value64Bit
{
	uint32_t LowPart;
	uint32_t HighPart;

} val64_t;

typedef struct Value128Bit
{
	uint32_t Part32;
	uint32_t Part64;
	uint32_t Part96;
	uint32_t Part128;

} val128_t;


#endif
