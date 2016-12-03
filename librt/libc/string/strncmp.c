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

int strncmp(const char *s1, const char *s2, size_t n)
{
	unsigned long *a1;
	unsigned long *a2;

	if (n == 0)
		return 0;

	/* If s1 or s2 are unaligned, then compare bytes. */
	if (!UNALIGNED (s1, s2))
	{
		/* If s1 and s2 are word-aligned, compare them a word at a time. */
		a1 = (unsigned long*)s1;
		a2 = (unsigned long*)s2;
		while (n >= sizeof (long) && *a1 == *a2)
		{
			n -= sizeof (long);

			/* If we've run out of bytes or hit a null, return zero
			since we already know *a1 == *a2.  */
			if (n == 0 || DETECTNULL (*a1))
				return 0;

			a1++;
			a2++;
		}

		/* A difference was detected in last few bytes of s1, so search bytewise */
		s1 = (char*)a1;
		s2 = (char*)a2;
	}

	while (n-- > 0 && *s1 == *s2)
	{
		/* If we've run out of bytes or hit a null, return zero
		since we already know *s1 == *s2.  */
		if (n == 0 || *s1 == '\0')
			return 0;

		s1++;
		s2++;
	}

	return (*(unsigned char *) s1) - (*(unsigned char *) s2);
}

//Old

//while ( *s1 && n && ( *s1 == *s2 ) )
//{
//	++s1;
//	++s2;
//	--n;
//}
//if ( ( n == 0 ) )
//{
//	return 0;
//}
//else
//{
//	return ( *(unsigned char *)s1 - *(unsigned char *)s2 );
//}