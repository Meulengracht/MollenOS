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

/* Write AT MOSt size bytes */
int snprintf(char *str, size_t size, const char *format, ...)
{
	va_list args;

	if(format == NULL || str == NULL)
		return -1;

	va_start( args, format );
	return streamout( &str, size, format, args );
}