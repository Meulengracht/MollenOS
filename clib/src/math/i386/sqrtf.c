/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER.PD within this package.
 */
#include <math.h>

#if (_MOLLENOS >= 0x100) && \
	(defined(__x86_64) || defined(_M_AMD64) || \
	defined (__ia64__) || defined (_M_IA64))
float
sqrtf(float x)
{
   return ((float)sqrt((double)x));
}
#else
float __emptysqrtf(void) { return 0.0f; }
#endif