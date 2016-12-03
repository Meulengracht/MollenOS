/*
 *     STRING
 *     Comparison
 */

#include <string.h>
#include <internal/_string.h>
#include <limits.h>

/* DETECTNULL returns nonzero if (long)X contains a NULL byte. */
#if LONG_MAX == 2147483647L
#define DETECTNULL(X) (((X) - 0x01010101) & ~(X) & 0x80808080)
#else
#if LONG_MAX == 9223372036854775807L
#define DETECTNULL(X) (((X) - 0x0101010101010101) & ~(X) & 0x8080808080808080)
#else
#error long int is not a 32bit or 64bit type.
#endif
#endif

#ifdef _MSC_VER
#pragma function(strcmp)
#endif

int strcmp(const char* str1, const char* str2)
{
	unsigned long *a1;
	unsigned long *a2;

	/* If s1 or s2 are unaligned, then compare bytes. */
	if (!UNALIGNED (str1, str2))
	{  
		/* If s1 and s2 are word-aligned, compare them a word at a time. */
		a1 = (unsigned long*)str1;
		a2 = (unsigned long*)str2;
		while (*a1 == *a2)
		{
			/* To get here, *a1 == *a2, thus if we find a null in *a1,
			then the strings must be equal, so return zero.  */
			if (DETECTNULL (*a1))
				return 0;

			a1++;
			a2++;
		}

		/* A difference was detected in last few bytes of s1, so search bytewise */
		str1 = (char*)a1;
		str2 = (char*)a2;
	}

	while (*str1 != '\0' && *str1 == *str2)
	{
		str1++;
		str2++;
	}
	return (*(unsigned char *) str1) - (*(unsigned char *) str2);
}

//Old
//for(; *str1 == *str2; ++str1, ++str2)
//	if(*str1 == 0)
//		return 0;
//return *(unsigned char *)str1 < *(unsigned char *)str2 ? -1 : 1;