/*
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS crt library
 * FILE:            lib/sdk/crt/printf/_sxprintf.c
 * PURPOSE:         Implementation of swprintf
 * PROGRAMMER:      Timo Kreuzer
 */

#include <internal/_file.h>
#include <internal/_io.h>
#include <os/osdefs.h>
#include <stdio.h>
#include <stdarg.h>
#include <limits.h>

#ifdef _UNICODE
#define _tstreamout wstreamout
#else
#define _tstreamout streamout
#endif

#define min(a,b) (((a) < (b)) ? (a) : (b))

int _sxprintf(
    _In_ TCHAR *buffer,
#if USE_COUNT
    _In_ size_t count,
#endif
    _In_ const TCHAR *format,
#if USE_VARARGS
    _In_ va_list argptr)
#else
    ...)
#endif
{
#if !USE_COUNT
    const size_t count = INT_MAX;
#endif
    const size_t sizeOfBuffer = count;
#if !USE_VARARGS
    va_list argptr;
#endif
    int result;
    FILE stream = { 0 };
    __FILE_Streamout(&stream, buffer, sizeOfBuffer * sizeof(TCHAR));

#if USE_VIRTUAL
    stream.Flags |= _IOVRT;
#endif

#if !USE_VARARGS
    va_start(argptr, format);
#endif
    result = _tstreamout(&stream, format, argptr);
#if !USE_VARARGS
    va_end(argptr);
#endif

    // Only zero terminate if there is enough space left
    if ((__FILE_BufferBytesForWriting(&stream) >= sizeof(TCHAR)) && (stream.Current)) {
        *(TCHAR*)stream.Current = _T('\0');
    }
    return result;
}


