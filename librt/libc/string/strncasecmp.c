/*
 *     STRING
 *     Comparison
 */

#include <strings.h>
#include <ctype.h>

int strncasecmp(const char *s1, const char *s2, size_t n)
{
	const unsigned char *ucs1 = (const unsigned char *) s1;
	const unsigned char *ucs2 = (const unsigned char *) s2;
	int d = 0;
	for ( ; n != 0; n--)
	{
		const int c1 = tolower(*ucs1);
		const int c2 = tolower(*ucs2);
		
		if (((d = c1 - c2) != 0) || (c2 == '\0'))
			break;

        ucs1++;
        ucs2++;
	}
	return d;
}
