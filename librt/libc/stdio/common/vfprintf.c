#include <stdio.h>
#include <stdarg.h>

int vfprintf(
    _In_ FILE *file, 
    _In_ __CONST char *format, 
    _In_ va_list argptr)
{
    int result;

    _lock_file(file);
    result = streamout(file, format, argptr);
    _unlock_file(file);

    return result;
}
