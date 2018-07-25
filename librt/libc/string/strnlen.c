/*
*     STRING
*     Other (strnlen)
*/

#include <string.h>
#include <stddef.h>

size_t strnlen(const char *str, size_t max)
{
    size_t cur = 0;
    if (str == NULL || max == 0) {
        return 0;
    }

    while (*str) {
        str++;
        cur++;
        if (cur == max) {
            break;
        }
    }
    return cur;
}
