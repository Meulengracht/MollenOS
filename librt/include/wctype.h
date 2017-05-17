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
#include <crtdefs.h>
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

_CRTIMP int	iswalpha(wint_t);
_CRTIMP int	iswalnum(wint_t);
_CRTIMP int	iswblank(wint_t);
_CRTIMP int	iswcntrl(wint_t);
_CRTIMP int	iswctype(wint_t, wctype_t);
_CRTIMP int	iswdigit(wint_t);
_CRTIMP int	iswgraph(wint_t);
_CRTIMP int	iswlower(wint_t);
_CRTIMP int	iswprint(wint_t);
_CRTIMP int	iswpunct(wint_t);
_CRTIMP int	iswspace(wint_t);
_CRTIMP int	iswupper(wint_t);
_CRTIMP int	iswxdigit(wint_t);
_CRTIMP wint_t towctrans(wint_t, wctrans_t);
_CRTIMP wint_t towupper(wint_t);
_CRTIMP wint_t towlower(wint_t);
_CRTIMP wctrans_t wctrans(__CONST char *);
_CRTIMP wctype_t wctype(__CONST char *);

#if defined(__POSIX_VISIBLE)
_CRTIMP int	iswalpha_l (wint_t, locale_t);
_CRTIMP int	iswalnum_l (wint_t, locale_t);
_CRTIMP int	iswblank_l (wint_t, locale_t);
_CRTIMP int	iswcntrl_l (wint_t, locale_t);
_CRTIMP int	iswctype_l (wint_t, wctype_t, locale_t);
_CRTIMP int	iswdigit_l (wint_t, locale_t);
_CRTIMP int	iswgraph_l (wint_t, locale_t);
_CRTIMP int	iswlower_l (wint_t, locale_t);
_CRTIMP int	iswprint_l (wint_t, locale_t);
_CRTIMP int	iswpunct_l (wint_t, locale_t);
_CRTIMP int	iswspace_l (wint_t, locale_t);
_CRTIMP int	iswupper_l (wint_t, locale_t);
_CRTIMP int	iswxdigit_l (wint_t, locale_t);
_CRTIMP wint_t	towctrans_l (wint_t, wctrans_t, locale_t);
_CRTIMP wint_t	towupper_l (wint_t, locale_t);
_CRTIMP wint_t	towlower_l (wint_t, locale_t);
_CRTIMP wctrans_t wctrans_l (__CONST char *, locale_t);
_CRTIMP wctype_t wctype_l (__CONST char *, locale_t);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _WCTYPE_H_ */
