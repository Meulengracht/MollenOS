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
    FILE stream;

    // Setup the FILE structure
    stream._base = (char*)buffer;
    stream._ptr = stream._base;
    stream._charbuf = 0;
    stream._cnt = (int)(sizeOfBuffer * sizeof(TCHAR));
    stream._bufsiz = 0;
    stream._flag = _IOSTRG | _IOWRT;
    stream._tmpfname = 0;
    usched_mtx_init(&stream._lock, USCHED_MUTEX_RECURSIVE);

#if USE_VIRTUAL
    stream._flag |= _IOVRT;
#endif

#if !USE_VARARGS
    va_start(argptr, format);
#endif
    result = _tstreamout(&stream, format, argptr);
#if !USE_VARARGS
    va_end(argptr);
#endif

    // Only zero terminate if there is enough space left
    if ((stream._cnt >= sizeof(TCHAR)) && (stream._ptr))
        *(TCHAR*)stream._ptr = _T('\0');

    return result;
}


