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
int vsnprintf(char *str, size_t size, const char *format, va_list ap)
{
	int result;
	result = streamout(&str, size, format, ap);
	return result;
}