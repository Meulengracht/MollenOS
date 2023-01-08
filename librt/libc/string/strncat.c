/*
 *     STRING
 *     Concenation
 */

#include <stdint.h>
#include <limits.h>
#include <string.h>

/* Nonzero if X is aligned on a "long" boundary.  */
#define ALIGNED(X) \
	(((long)(intptr_t)(X) & (sizeof (long) - 1)) == 0)

#if LONG_MAX == 2147483647L
#define DETECTNULL(X) (((X) - 0x01010101) & ~(X) & 0x80808080)
#else
#if LONG_MAX == 9223372036854775807L
/* Nonzero if X (a long int) contains a NULL byte. */
#define DETECTNULL(X) (((X) - 0x0101010101010101) & ~(X) & 0x8080808080808080)
#else
#error long int is not a 32bit or 64bit type.
#endif
#endif

char* strncat (char* destination, const char* source, size_t num)
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

	/* s1 now points to the its trailing null character, now copy
		up to N bytes from S2 into S1 stopping if a NULL is encountered
		in S2.

		It is not safe to use strncpy here since it copies EXACTLY N
		characters, NULL padding if necessary.  */
	while (num-- != 0 && (*destination++ = *source++))
	{
		if (num == 0)
			*destination = '\0';
	}
	
	return s;
}

//Old

//char *dp = destination;
//if(dp == NULL)
//	strncpy(destination, source, num);
//else
//{
//	size_t i;
//	if(strlen(source) < num)
//		num = strlen(source);
//
//	for(i = 0; i < strlen(destination); i++)
//	{
//		if(dp[i] == '\0' || dp[i] == 0)
//		{
//			size_t p = 0;
//			size_t j;
//			for(j = 0; j < num; j++)
//			{
//				dp[i + p] = source[j];
//				p++;
//			}
//			dp[i + p] = '\0';
//			return destination;
//		}
//	}
//
//}
//return destination;