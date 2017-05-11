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

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(strcpy)
#endif

char* strcpy(char *to, const char *from)
{
	char *dst = to;
	const char *src = from;
	long *aligned_dst;
	const long *aligned_src;

	/* If SRC or DEST is unaligned, then copy bytes.  */
	if (!UNALIGNED (src, dst))
	{
		aligned_dst = (long*)dst;
		aligned_src = (long*)src;

		/* SRC and DEST are both "long int" aligned, try to do "long int"
			sized copies.  */
		while (!DETECTNULL(*aligned_src))
		{
			*aligned_dst++ = *aligned_src++;
		}

		dst = (char*)aligned_dst;
		src = (char*)aligned_src;
	}

	while ((*dst++ = *src++))
	;
	return to;
}