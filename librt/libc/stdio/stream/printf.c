#include <stdio.h>
#include <stdarg.h>

int printf(
    _In_ __CONST char *format, 
    ...)
{
    va_list argptr;
    int result;

    va_start(argptr, format);
    result = vfprintf(stdout, format, argptr);
    va_end(argptr);

    return result;
}
