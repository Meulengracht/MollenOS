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
* MollenOS C Library - Standard Library Wide Conversion Macros
*/

#ifndef _WCTYPE_H_
#define _WCTYPE_H_

/* Includes */
#include <stdint.h>
#include <stddef.h>

#ifndef WEOF
# define WEOF ((wint_t)-1)
#endif

/* CPP Guard */
#ifdef __cplusplus
extern "C"
{
#endif

#ifndef _WCTYPE_T_DEFINED
#define _WCTYPE_T_DEFINED
	typedef unsigned short wint_t;
	typedef unsigned short wctype_t;
#endif

#ifndef _WCTRANS_T
#define _WCTRANS_T
typedef int wctrans_t;
#endif

_CRT_EXTERN int	iswalpha(wint_t);
_CRT_EXTERN int	iswalnum(wint_t);
_CRT_EXTERN int	iswblank(wint_t);
_CRT_EXTERN int	iswcntrl(wint_t);
_CRT_EXTERN int	iswctype(wint_t, wctype_t);
_CRT_EXTERN int	iswdigit(wint_t);
_CRT_EXTERN int	iswgraph(wint_t);
_CRT_EXTERN int	iswlower(wint_t);
_CRT_EXTERN int	iswprint(wint_t);
_CRT_EXTERN int	iswpunct(wint_t);
_CRT_EXTERN int	iswspace(wint_t);
_CRT_EXTERN int	iswupper(wint_t);
_CRT_EXTERN int	iswxdigit(wint_t);
_CRT_EXTERN wint_t towctrans(wint_t, wctrans_t);
_CRT_EXTERN wint_t towupper(wint_t);
_CRT_EXTERN wint_t towlower(wint_t);
_CRT_EXTERN wctrans_t wctrans(const char *);
_CRT_EXTERN wctype_t wctype(const char *);

#if defined(__POSIX_VISIBLE)
extern int	iswalpha_l (wint_t, locale_t);
extern int	iswalnum_l (wint_t, locale_t);
extern int	iswblank_l (wint_t, locale_t);
extern int	iswcntrl_l (wint_t, locale_t);
extern int	iswctype_l (wint_t, wctype_t, locale_t);
extern int	iswdigit_l (wint_t, locale_t);
extern int	iswgraph_l (wint_t, locale_t);
extern int	iswlower_l (wint_t, locale_t);
extern int	iswprint_l (wint_t, locale_t);
extern int	iswpunct_l (wint_t, locale_t);
extern int	iswspace_l (wint_t, locale_t);
extern int	iswupper_l (wint_t, locale_t);
extern int	iswxdigit_l (wint_t, locale_t);
extern wint_t	towctrans_l (wint_t, wctrans_t, locale_t);
extern wint_t	towupper_l (wint_t, locale_t);
extern wint_t	towlower_l (wint_t, locale_t);
extern wctrans_t wctrans_l (const char *, locale_t);
extern wctype_t wctype_l (const char *, locale_t);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WCTYPE_H_ */
