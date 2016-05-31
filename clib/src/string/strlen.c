/*
 *     STRING
 *     Other (strlen)
 */

#include <string.h>
#include <stdint.h>
#include <limits.h>

#define LBLOCKSIZE   (sizeof (long))
#define UNALIGNED(X) ((long)X & (LBLOCKSIZE - 1))

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

#ifndef DETECTNULL
#error long int is not a 32bit or 64bit byte
#endif

#ifdef _MSC_VER
#pragma function(strlen)
#endif

size_t strlen(const char *str)
{
	const char *start = str;
	unsigned long *aligned_addr;

	/* Align the pointer, so we can search a word at a time.  */
	while (UNALIGNED (str))
	{
		if (!*str)
			return str - start;
		str++;
	}

	/* If the string is word-aligned, we can check for the presence of
		a null in each word-sized block.  */
	aligned_addr = (unsigned long *)str;
	while (!DETECTNULL (*aligned_addr))
	aligned_addr++;

	/* Once a null is detected, we check each byte in that block for a
		precise position of the null.  */
	str = (char *) aligned_addr;

	while (*str)
		str++;

	return str - start;
}