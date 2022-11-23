#include <stdio.h>
#include <stdarg.h>

int vprintf(
    _In_ const char *format,
    _In_ va_list argptr)
{
    return vfprintf(stdout, format, argptr);
}
