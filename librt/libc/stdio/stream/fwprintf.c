#include <stdio.h>
#include <stdarg.h>

int fwprintf(
    _In_ FILE* file, 
    _In_ const wchar_t *format,
    ...)
{
    va_list argptr;
    int result;

    va_start(argptr, format);
    result = vfwprintf(file, format, argptr);
    va_end(argptr);
    return result;
}

