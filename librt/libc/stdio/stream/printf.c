#include <stdio.h>
#include <stdarg.h>

int printf(
    _In_ const char *format,
    ...)
{
    va_list argptr;
    int result;

    va_start(argptr, format);
    result = vfprintf(stdout, format, argptr);
    va_end(argptr);

    return result;
}
