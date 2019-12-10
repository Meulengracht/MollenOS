#include <stdio.h>
#include <stdarg.h>

int vfwprintf(
    _In_ FILE* file, 
    _In_ __CONST wchar_t *format, 
    _In_ va_list argptr)
{
    int ret;

    _lock_file(file);
    ret = wstreamout(file, format, argptr);
    _unlock_file(file);

    return ret;
}
