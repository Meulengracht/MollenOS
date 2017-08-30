#include <stdio.h>
#include <stdarg.h>

int
_vcprintf(
    _In_ __CONST char* format, 
    _In_ va_list va)
{
    return vfprintf(stdout, format, va);
}
