/*
*     STRING
*     Other (strnlen)
*/

#include <string.h>
#include <stddef.h>

/* Checks the length of a string 
 * with an upper bound in case of 
 * veeeeery long strings */
size_t strnlen(const char *str, size_t max)
{
	size_t cur = 0;

	/* Sanity */
	if (str == NULL
		|| max == 0)
		return 0;

	while (*str) {
		str++;
		cur++;
		if (cur == max) {
			break;
		}
	}
		
	return cur;
}