#include <stdio.h>
#include <stdarg.h>

int _vcwprintf(
    _In_ __CONST wchar_t* format, 
    _In_ va_list va)
{
    return vfwprintf(stdout, format, va);
}
