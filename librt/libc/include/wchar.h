/**
 * Copyright 2022, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _WCHAR_H_
#define _WCHAR_H_

#define __need_wchar_t
#define __need_size_t
#define __need_wint_t
#define __need_NULL
#define __need_tm
#define __need_mbstate
#include <crtdefs.h>
#include <stddef.h>
#include <_types.h>
#include <stdarg.h>
#include <stdint.h>

#ifndef WEOF
# define WEOF ((wint_t)-1)
#endif

_CODE_BEGIN

/* mbsinit
 * If ps is not a null pointer, the mbsinit function determines whether the pointed-to 
 * mbstate_t object describes the initial conversion state. */
CRTDECL(int, mbsinit(
    _In_ const mbstate_t *ps));

CRTDECL(wchar_t*, wcpcpy(wchar_t *s1, const wchar_t *s2));
CRTDECL(wchar_t*, wcpncpy(wchar_t *__restrict dst, const wchar_t *__restrict src, size_t count));
CRTDECL(int,      wcscasecmp(const wchar_t *s1, const wchar_t *s2));
CRTDECL(wchar_t*, wmemset(wchar_t *s, wchar_t c, size_t n));
CRTDECL(wchar_t*, wmemmove(wchar_t *d, const wchar_t *s, size_t n));
CRTDECL(wchar_t*, wmemcpy(wchar_t *__restrict d, const wchar_t *__restrict s, size_t n));
CRTDECL(int,      wmemcmp(const wchar_t *s1, const wchar_t *s2, size_t n));
CRTDECL(wchar_t*, wmemchr(const wchar_t *s, wchar_t c, size_t n));
CRTDECL(int,      wcwidth(const wchar_t wc));
CRTDECL(int,      wcswidth(const wchar_t *pwcs, size_t n));
CRTDECL(size_t,   wcslcpy(wchar_t *dst, const wchar_t *src, size_t siz));
CRTDECL(size_t,   wcsxfrm(wchar_t *__restrict a, const wchar_t *__restrict b, size_t n));
CRTDECL(wchar_t*, wcscat(wchar_t *__restrict s1, const wchar_t *__restrict s2));
CRTDECL(wchar_t*, wcschr(const wchar_t *s, wchar_t c));
CRTDECL(int,      wcscmp(const wchar_t *s1, const wchar_t *s2));
CRTDECL(int,      wcscoll(const wchar_t *a, const wchar_t *b));
CRTDECL(wchar_t*, wcscpy(wchar_t *__restrict s1, const wchar_t *__restrict s2));
CRTDECL(size_t,   wcscspn(const wchar_t *s, wchar_t *set));
CRTDECL(wchar_t*, wcsdup(const wchar_t *str));
CRTDECL(size_t,   wcslcat(wchar_t *dst, const wchar_t *src, size_t siz));
CRTDECL(size_t,   wcslen(const wchar_t *s));
CRTDECL(size_t,   wcsnlen(const wchar_t *s, size_t maxlen));
CRTDECL(int,      wcsncasecmp(const wchar_t *s1, const wchar_t *s2, size_t n));
CRTDECL(wchar_t*, wcsncat(wchar_t *__restrict s1, const wchar_t *__restrict s2, size_t n));
CRTDECL(int,      wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n));
CRTDECL(wchar_t*, wcsncpy(wchar_t *__restrict s1, const wchar_t *__restrict s2, size_t n));
CRTDECL(wchar_t*, wcspbrk(const wchar_t *s, const wchar_t *set));
CRTDECL(wchar_t*, wcsrchr(const wchar_t *s, wchar_t c));
CRTDECL(size_t,   wcsspn(const wchar_t *s, const wchar_t *set));
CRTDECL(wchar_t*, wcsstr(const wchar_t *__restrict big, const wchar_t *__restrict little));
CRTDECL(wchar_t*, wcstok(wchar_t *__restrict source, const wchar_t *__restrict delimiters, wchar_t **__restrict lasts));

/**
 * @brief Locale variants.
 */
#include <locale.h>
CRTDECL(int, wcsxfrm_l(wchar_t *__restrict a, const wchar_t *__restrict b, size_t n, locale_t locale));
CRTDECL(int, wcscasecmp_l(const wchar_t *s1, const wchar_t *s2, locale_t locale));
CRTDECL(int, wcscoll_l(const wchar_t *a, const wchar_t *b, locale_t locale));
CRTDECL(int, wcsncasecmp_l(const wchar_t *s1, const wchar_t *s2, size_t n, locale_t locale));

/* wcsftime
 * Converts the date and time information from a given calendar time
 * to a null-terminated wide character string str according to format string format. 
 * Up to count bytes are written. */
CRTDECL(size_t, wcsftime(
    _In_ wchar_t*__restrict str,
    _In_ size_t maxsize,
    _In_ const wchar_t*__restrict format, 
    _In_ const struct tm*__restrict time));
CRTDECL(size_t, wcsftime_l(
    _In_ wchar_t*__restrict str,
    _In_ size_t maxsize,
    _In_ const wchar_t*__restrict format, 
    _In_ const struct tm*__restrict time,
    _In_ struct __locale_t *locale));

_CODE_END
#endif /* _WCHAR_H_ */
