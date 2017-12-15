/*
 *     STRING
 *     Comparison
 */

#include <string.h>

int
strcoll_l (const char *a, const char *b, struct __locale_t *locale)
{
  return strcmp (a, b);
}

int strcoll(const char* str1, const char* str2)
{
	return strcmp(str1, str2);
}