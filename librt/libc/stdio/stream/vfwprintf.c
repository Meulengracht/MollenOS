
#include <internal/_io.h>
#include <stdio.h>
#include <stdarg.h>

int vfwprintf(
    _In_ FILE*          file,
    _In_ const wchar_t* format,
    _In_ va_list        argptr)
{
    int ret;

    flockfile(file);
    ret = wstreamout(file, format, argptr);
    funlockfile(file);

    return ret;
}
