/*
*     STRING
*     Other (strlen)
*/

#include <string.h>
#include <stdint.h>
#include <limits.h>

size_t wcsnlen(const wchar_t * str, size_t count)
{
	const wchar_t * s;

	if (str == 0) return 0;

	for (s = str; *s && count; ++s, --count);

	return s - str;
}