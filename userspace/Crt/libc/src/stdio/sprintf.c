/*
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS crt library
 * FILE:            lib/sdk/crt/printf/streamout.c
 * PURPOSE:         Implementation of streamout
 * PROGRAMMER:      Timo Kreuzer
 */

#include <stdio.h>
#include <stddef.h>

//Extern
extern int _cdecl streamout(char **, size_t size, const char *, va_list);

int sprintf(char *out, const char *format, ...)
{
	va_list args;

	if(format == NULL || out == NULL)
		return -1;

	va_start( args, format );
	return streamout( &out, 0x200000, format, args );
}