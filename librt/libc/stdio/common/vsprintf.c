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

int vsprintf(char *str, const char *format, va_list ap)
{
	int result;
	result = streamout(&str, 0x20000, format, ap);
	return result;
}