/*
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS crt library
 * FILE:            lib/sdk/crt/printf/_vscwprintf.c
 * PURPOSE:         Implementation of _vscprintf
 */

#include <stdio.h>
#include <stdarg.h>

int _vscwprintf(
    __CONST wchar_t *format,
   va_list argptr)
{
    int ret;
    FILE* nulfile;
    nulfile = fopen("nul", "w");
    if(nulfile == NULL)
    {
        /* This should never happen... */
        return -1;
    }
    ret = wstreamout(nulfile, format, argptr);
    fclose(nulfile);
    return ret;
}
