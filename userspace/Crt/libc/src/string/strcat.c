/*
 *     STRING
 *     Concenation
 */

#include <string.h>
#include <limits.h>

/* Nonzero if X is aligned on a "long" boundary.  */
#define ALIGNED(X) \
	(((long)X & (sizeof (long) - 1)) == 0)

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

#pragma function(strcat)
char* strcat (char* destination, const char* source)
{
	char *s = destination;

	/* Skip over the data in s1 as quickly as possible.  */
	if (ALIGNED (destination))
	{
		unsigned long *aligned_s1 = (unsigned long *)destination;
		
		while (!DETECTNULL (*aligned_s1))
			aligned_s1++;

		destination = (char *)aligned_s1;
	}

	while (*destination)
		destination++;

	/* s1 now points to the its trailing null character, we can
		just use strcpy to do the work for us now. */
	strcpy(destination, source);
	
	return s;
}