#include <string.h>

/*
 * @implemented
 */
long atol(const char *str)
{
    return (long)atoi64(str);
}