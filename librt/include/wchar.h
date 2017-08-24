/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation ? , either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS C Library - Standard WChar header
*/

#ifndef _WCHAR_H_
#define _WCHAR_H_

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <stddef.h>
#include <stdarg.h>

#ifndef WEOF
# define WEOF ((wint_t)-1)
#endif

/* This must match definition in <stdint.h> */
#ifndef WCHAR_MIN
#ifdef __WCHAR_MIN__
#define WCHAR_MIN __WCHAR_MIN__
#elif defined(__WCHAR_UNSIGNED__) || (L'\0' - 1 > 0)
#define WCHAR_MIN (0 + L'\0')
#else
#define WCHAR_MIN (-0x7fffffff - 1 + L'\0')
#endif
#endif

/* This must match definition in <stdint.h> */
#ifndef WCHAR_MAX
#ifdef __WCHAR_MAX__
#define WCHAR_MAX __WCHAR_MAX__
#elif defined(__WCHAR_UNSIGNED__) || (L'\0' - 1 > 0)
#define WCHAR_MAX (0xffffffffu + L'\0')
#else
#define WCHAR_MAX (0x7fffffff + L'\0')
#endif
#endif

/* Cpp-Guard */
#ifdef __cplusplus
extern "C" {
#endif

#ifndef _MBSTATE_T
#define _MBSTATE_T
/* Conversion state information.  */
typedef struct
{
	int __count;
	union
	{
		wint_t __wch;
		unsigned char __wchb[4];
	} __val;		/* Value so far.  */
} _mbstate_t;
typedef _mbstate_t mbstate_t;
#endif /* _MBSTATE_T */

#ifndef __VALIST
#ifdef __GNUC__
#define __VALIST __gnuc_va_list
#else
#define __VALIST char*
#endif
#endif

_CRTIMP wchar_t *wcpcpy(wchar_t *s1, __CONST wchar_t *s2);
_CRTIMP wchar_t *wcpncpy(wchar_t *__restrict dst,
	__CONST wchar_t *__restrict src, size_t count);
_CRTIMP int wcscasecmp(__CONST wchar_t *s1, __CONST wchar_t *s2);
_CRTIMP wchar_t *wmemset(wchar_t *s, wchar_t c, size_t n);
_CRTIMP wchar_t *wmemmove(wchar_t *d, __CONST wchar_t *s, size_t n);
_CRTIMP wchar_t *wmemcpy(wchar_t *__restrict d,
	__CONST wchar_t *__restrict s, size_t n);
_CRTIMP int wmemcmp(__CONST wchar_t *s1, __CONST wchar_t *s2, size_t n);
_CRTIMP wchar_t	*wmemchr(__CONST wchar_t *s, wchar_t c, size_t n);
_CRTIMP int wcwidth(__CONST wchar_t wc);
_CRTIMP int wcswidth(__CONST wchar_t *pwcs, size_t n);
_CRTIMP size_t wcslcpy(wchar_t *dst, __CONST wchar_t *src, size_t siz);
_CRTIMP size_t wcsxfrm(wchar_t *__restrict a,
	__CONST wchar_t *__restrict b, size_t n);
_CRTIMP wchar_t *wcscat(wchar_t *__restrict s1,
	__CONST wchar_t *__restrict s2);
_CRTIMP wchar_t *wcschr(__CONST wchar_t *s, wchar_t c);
_CRTIMP int wcscmp(__CONST wchar_t *s1, __CONST wchar_t *s2);
_CRTIMP int wcscoll(__CONST wchar_t *a, __CONST wchar_t *b);
_CRTIMP wchar_t *wcscpy(wchar_t *__restrict s1,
	__CONST wchar_t *__restrict s2);
_CRTIMP	size_t wcscspn(__CONST wchar_t *s, wchar_t *set);
_CRTIMP wchar_t *wcsdup(__CONST wchar_t *str);
_CRTIMP size_t wcslcat(wchar_t *dst, 
	__CONST wchar_t *src, size_t siz);
_CRTIMP size_t wcslen(__CONST wchar_t *s);
_CRTIMP size_t wcsnlen(__CONST wchar_t *s, size_t maxlen);
_CRTIMP int wcsncasecmp(__CONST wchar_t *s1, 
	__CONST wchar_t *s2, size_t n);
_CRTIMP wchar_t *wcsncat(wchar_t *__restrict s1,
	__CONST wchar_t *__restrict s2, size_t n);
_CRTIMP int wcsncmp(__CONST wchar_t *s1, 
	__CONST wchar_t *s2, size_t n);
_CRTIMP wchar_t *wcsncpy(wchar_t *__restrict s1,
	__CONST wchar_t *__restrict s2, size_t n);
_CRTIMP wchar_t *wcspbrk(__CONST wchar_t *s,
	__CONST wchar_t *set);
_CRTIMP wchar_t *wcsrchr(__CONST wchar_t *s, wchar_t c);
_CRTIMP size_t wcsspn(__CONST wchar_t *s, __CONST wchar_t *set);
_CRTIMP wchar_t *wcsstr(__CONST wchar_t *__restrict big,
	__CONST wchar_t *__restrict little);
_CRTIMP wchar_t *wcstok(wchar_t *__restrict source,
	__CONST wchar_t *__restrict delimiters,
	wchar_t **__restrict lasts);

#if defined(__POSIX_VISIBLE)
#include <locale.h>
_CRTIMP int wcsxfrm_l(wchar_t *__restrict a,
	__CONST wchar_t *__restrict b, size_t n,
	locale_t locale);
_CRTIMP int wcscasecmp_l(__CONST wchar_t *s1, 
	__CONST wchar_t *s2, locale_t locale);
_CRTIMP int wcscoll_l(__CONST wchar_t *a, 
	__CONST wchar_t *b, locale_t locale);
_CRTIMP int wcsncasecmp_l(__CONST wchar_t *s1, 
	__CONST wchar_t *s2, size_t n, 
	locale_t locale);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WCHAR_H_ */
