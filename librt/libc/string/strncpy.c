/*
 *     STRING
 *     Copying
 */

#include <string.h>
#include <stdint.h>
#include <internal/_string.h>
#include <limits.h>

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

char* strncpy(char* destination, const char* source, size_t num)
{
	char *dst = destination;
	const char *src = source;
	long *aligned_dst;
	const long *aligned_src;

	/* If SRC and DEST is aligned and count large enough, then copy words.  */
	if (!UNALIGNED (src, dst) && !TOO_SMALL (num))
	{
		aligned_dst = (long*)dst;
		aligned_src = (long*)src;

		/* SRC and DEST are both "long int" aligned, try to do "long int"
		sized copies.  */
		while (num >= sizeof (long int) && !DETECTNULL(*aligned_src))
	{
		num -= sizeof (long int);
		*aligned_dst++ = *aligned_src++;
	}

		dst = (char*)aligned_dst;
		src = (char*)aligned_src;
	}

	while (num > 0)
	{
		--num;
		if ((*dst++ = *src++) == '\0')
	break;
	}

	while (num-- > 0)
	*dst++ = '\0';

	return destination;
}

//Old

//size_t t = strlen(source);
//char *dp = destination;
//size_t i;

//if(t > num)
//	num = t;

//for(i = 0; i < num; i++)
//	dp[i] = source[i];

//return destination;