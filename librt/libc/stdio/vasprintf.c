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

int vasprintf(char **ret, const char *format, va_list ap)
{
	if(format == NULL || ret == NULL)
		return -1;

	return streamout(ret, 0x200000, format, ap);
}