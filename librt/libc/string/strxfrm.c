/*
 *     STRING
 *     Transformation
 */

#include <string.h>

size_t strxfrm(char* destination, const char* source, size_t num)
{
	size_t res;
	res = 0;

	while (num-- > 0)
	{
		if ((*destination++ = *source++) != '\0')
			++res;
		else
			return res;
	}

	while (*source)
	{
		++source;
		++res;
	}

	return res;
}