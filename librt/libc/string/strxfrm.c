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

size_t
strxfrm_l (char *__restrict s1, const char *__restrict s2, size_t n,
	   struct __locale_t *locale)
{
  size_t res;
  res = 0;
  while (n-- > 0)
    {
      if ((*s1++ = *s2++) != '\0')
        ++res;
      else
        return res;
    }
  while (*s2)
    {
      ++s2;
      ++res;
    }

  return res;
}
