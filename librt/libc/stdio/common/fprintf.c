#include <stdio.h>
#include <stdarg.h>

int fprintf(
    _In_ FILE *file, 
    _In_ __CONST char *format, 
    ...)
{
    va_list argptr;
    int result;

    va_start(argptr, format);
    result = vfprintf(file, format, argptr);
    va_end(argptr);
    return result;
}
