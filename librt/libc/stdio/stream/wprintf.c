#include <stdio.h>
#include <stdarg.h>

int wprintf(
    _In_ const wchar_t *format,
    ...)
{
    va_list argptr;
    int result;

    va_start(argptr, format);
    result = vwprintf(format, argptr);
    va_end(argptr);
    return result;
}
