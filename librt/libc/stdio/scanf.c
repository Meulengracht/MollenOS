/*
 * general implementation of scanf used by scanf, sscanf, fscanf,
 * _cscanf, wscanf, swscanf and fwscanf
 *
 * Copyright 1996,1998 Marcus Meissner
 * Copyright 1996 Jukka Iivonen
 * Copyright 1997,2000 Uwe Bonnes
 * Copyright 2000 Jon Griffiths
 * Copyright 2002 Daniel Gudbjartsson
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "internal/_io.h"
#include "internal/_locale.h"
#include "ds/bitmap.h"
#include "wchar.h"
#include <stdarg.h>
#include <limits.h>
#include "stdio.h"
#include <stdint.h>
#include "locale.h"
#include "ctype.h"
#include "wctype.h"
#include "math.h"

/* helper function for *scanf.  Returns the value of character c in the
 * given base, or -1 if the given character is not a digit of the base.
 */
static int char2digit(char c, int base) {
    if ((c>='0') && (c<='9') && (c<='0'+base-1)) return (c-'0');
    if (base<=10) return -1;
    if ((c>='A') && (c<='Z') && (c<='A'+base-11)) return (c-'A'+10);
    if ((c>='a') && (c<='z') && (c<='a'+base-11)) return (c-'a'+10);
    return -1;
}

/* helper function for *wscanf.  Returns the value of character c in the
 * given base, or -1 if the given character is not a digit of the base.
 */
static int wchar2digit(wchar_t c, int base) {
    if ((c>='0') && (c<='9') && (c<='0'+base-1)) return (c-'0');
    if (base<=10) return -1;
    if ((c>='A') && (c<='Z') && (c<='A'+base-11)) return (c-'A'+10);
    if ((c>='a') && (c<='z') && (c<='a'+base-11)) return (c-'a'+10);
    return -1;
}

/* vfscanf_l */
#undef WIDE_SCANF
#undef CONSOLE
#undef STRING
#undef SECURE
#include "scanf.h"

/* vfwscanf_l */
#define WIDE_SCANF 1
#undef CONSOLE
#undef STRING
#undef SECURE
#include "scanf.h"

/* vcwscanf_l */
#define WIDE_SCANF 1
#define CONSOLE 1
#undef STRING
#undef SECURE
#include "scanf.h"

/* vsscanf_l */
#undef WIDE_SCANF
#undef CONSOLE
#define STRING 1
#undef SECURE
#include "scanf.h"

/* vswscanf_l */
#define WIDE_SCANF 1
#undef CONSOLE
#define STRING 1
#undef SECURE
#include "scanf.h"

/* vsnscanf_l */
#undef WIDE_SCANF
#undef CONSOLE
#define STRING 1
#define STRING_LEN 1
#undef SECURE
#include "scanf.h"

/* vcscanf_l */
#undef WIDE_SCANF
#define CONSOLE 1
#undef STRING
#undef SECURE
#include "scanf.h"

int fscanf(
    _In_ FILE *file, 
    _In_ const char *format,
    ...)
{
    va_list valist;
    int res;

    va_start(valist, format);
    res = vfscanf_l(file, format, NULL, valist);
    va_end(valist);
    return res;
}

int scanf(
    _In_ const char *format,
    ...)
{
    va_list valist;
    int res;

    va_start(valist, format);
    res = vfscanf_l(stdin, format, NULL, valist);
    va_end(valist);
    return res;
}

int fwscanf(
    _In_ FILE *file, 
    _In_ const wchar_t *format,
    ...)
{
    va_list valist;
    int res;

    va_start(valist, format);
    res = vfwscanf_l(file, format, NULL, valist);
    va_end(valist);
    return res;
}

int wscanf(
    _In_ const wchar_t *format,
    ...)
{
    va_list valist;
    int res;

    va_start(valist, format);
    res = vfwscanf_l(stdin, format, NULL, valist);
    va_end(valist);
    return res;
}

int sscanf(
    _In_ const char *str,
    _In_ const char *format,
    ...)
{
    va_list valist;
    int res;

    va_start(valist, format);
    res = vsscanf_l(str, format, NULL, valist);
    va_end(valist);
    return res;
}

int swscanf(
    _In_ const wchar_t *str,
    _In_ const wchar_t *format,
    ...)
{
    va_list valist;
    int res;

    va_start(valist, format);
    res = vswscanf_l(str, format, NULL, valist);
    va_end(valist);
    return res;
}

int vscanf(
    _In_ const char *format,
    _In_ va_list vlist)
{
    return vcscanf_l(format, NULL, vlist);
}

int vfscanf(
    FILE *stream,
    const char *format,
    va_list vlist)
{
    return vfscanf_l(stream, format, NULL, vlist);
}

int vsscanf(
    const char *buffer,
    const char *format,
    va_list vlist )
{
    return vsscanf_l(buffer, format, NULL, vlist);
}

int vwscanf(
    _In_ const wchar_t *format,
    _In_ va_list vlist)
{
    return vcwscanf_l(format, NULL, vlist);
}

int vfwscanf(
    FILE *stream,
    const wchar_t *format,
    va_list vlist )
{
    return vfwscanf_l(stream, format, NULL, vlist);
}

int vswscanf(
    const wchar_t *buffer,
    const wchar_t *format,
    va_list vlist )
{
    return vswscanf_l(buffer, format, NULL, vlist);
}

int _cscanf(const char *format, ...)
{
    va_list valist;
    int res;

    va_start(valist, format);
    res = vcscanf_l(format, NULL, valist);
    va_end(valist);
    return res;
}

int _snscanf(const char *input, size_t length, const char *format, ...)
{
    va_list valist;
    int res;

    va_start(valist, format);
    res = vsnscanf_l(input, length, format, NULL, valist);
    va_end(valist);
    return res;
}
