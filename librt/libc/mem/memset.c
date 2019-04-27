/*
 *     STRING
 *     Other (Memset)
 */

#include <string.h>

#define LBLOCKSIZE (sizeof(long))
#define UNALIGNED(X)   ((long)X & (LBLOCKSIZE - 1))
#define TOO_SMALL(LEN) ((LEN) < LBLOCKSIZE)

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memset)
#endif

void *memset(void *dest, int c, size_t count)
{
	char *s = (char *)dest;
	int i;
	unsigned long buffer;
	unsigned long *aligned_addr;
	unsigned int d = c & 0xff;	/* To avoid sign extension, copy C to an
					unsigned variable.  */
					
	while (UNALIGNED (s))
	{
		if (count--)
			*s++ = (char) c;
		else
			return dest;
	}

	if (!TOO_SMALL (count))
	{
		/* If we get this far, we know that n is large and s is word-aligned. */
		aligned_addr = (unsigned long *) s;

		/* Store D into each char sized location in BUFFER so that
			we can set large blocks quickly.  */
		buffer = (d << 8) | d;
		buffer |= (buffer << 16);
		for (i = 32; i < LBLOCKSIZE * 8; i <<= 1)
			buffer = (buffer << i) | buffer;

		/* Unroll the loop.  */
		while (count >= LBLOCKSIZE*4)
		{
			*aligned_addr++ = buffer;
			*aligned_addr++ = buffer;
			*aligned_addr++ = buffer;
			*aligned_addr++ = buffer;
			count -= 4*LBLOCKSIZE;
		}

		while (count >= LBLOCKSIZE)
		{
			*aligned_addr++ = buffer;
			count -= LBLOCKSIZE;
		}
		/* Pick up the remainder with a bytewise loop.  */
		s = (char*)aligned_addr;
	}

	while (count--)
		*s++ = (char) c;

	return dest;
}