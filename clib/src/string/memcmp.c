/*
 *     STRING
 *     Comparison
 */

#include <string.h>

/* Nonzero if either X or Y is not aligned on a "long" boundary.  */
#define MEMCMP_UNALIGNED(X, Y) \
	(((long)X & (sizeof (long) - 1)) | ((long)Y & (sizeof (long) - 1)))

/* How many bytes are copied each iteration of the word copy loop.  */
#define LBLOCKSIZE (sizeof (long))

/* Threshhold for punting to the byte copier.  */
#define TOO_SMALL(LEN)  ((LEN) < LBLOCKSIZE)

#ifdef _MSC_VER
#pragma function(memcmp)
#endif

int memcmp(const void* ptr1, const void* ptr2, size_t num)
{
	unsigned char *s1 = (unsigned char *) ptr1;
	unsigned char *s2 = (unsigned char *) ptr2;
	unsigned long *a1;
	unsigned long *a2;

	/* If the size is too small, or either pointer is unaligned,
		then we punt to the byte compare loop.  Hopefully this will
		not turn up in inner loops.  */
	if (!TOO_SMALL(num) && !MEMCMP_UNALIGNED(s1, s2))
	{
		/* Otherwise, load and compare the blocks of memory one 
			word at a time.  */
		a1 = (unsigned long*) s1;
		a2 = (unsigned long*) s2;
		while (num >= LBLOCKSIZE)
		{
			if (*a1 != *a2) 
   				break;

			a1++;
			a2++;
			num -= LBLOCKSIZE;
		}

		/* check m mod LBLOCKSIZE remaining characters */

		s1 = (unsigned char*)a1;
		s2 = (unsigned char*)a2;
	}

	while (num--)
	{
		if (*s1 != *s2)
			return *s1 - *s2;

		s1++;
		s2++;
	}

	return 0;
}