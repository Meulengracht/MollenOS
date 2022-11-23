
#include <internal/_io.h>
#include <stdio.h>
#include <stdarg.h>

int vfprintf(
    _In_ FILE *file, 
    _In_ const char *format,
    _In_ va_list argptr)
{
    int result;

    _lock_stream(file);
    result = streamout(file, format, argptr);
    _unlock_stream(file);

    return result;
}
