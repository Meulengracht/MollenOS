#include <stdarg.h>
#include <stdio.h>

__EXTERN
int
_vcprintf(
    _In_ __CONST char* format, 
    _In_ va_list va);

int
_cprintf(
    _In_ __CONST char * format, 
    ...)
{
    va_list argptr;
    int result;

    va_start(argptr, format);
    result = _vcprintf(format, argptr);
    va_end(argptr);
    return result;
}
