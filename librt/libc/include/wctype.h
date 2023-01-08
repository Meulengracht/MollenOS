/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _WCTYPE_H_
#define _WCTYPE_H_

#include <crtdefs.h>
#include <locale.h>

#ifndef _WINT_T 
#define _WINT_T 
#ifndef __WINT_TYPE__ 
#define __WINT_TYPE__ unsigned int 
#endif 
typedef __WINT_TYPE__ wint_t; 
#endif

#ifndef WEOF
# define WEOF ((wint_t)-1)
#endif

#ifndef _WCTYPE_T_DEFINED
#define _WCTYPE_T_DEFINED
typedef unsigned short wctype_t;
#endif

#ifndef _WCTRANS_T
#define _WCTRANS_T
typedef int wctrans_t;
#endif

_CODE_BEGIN
CRTDECL(int, iswalpha(wint_t));
CRTDECL(int, iswalnum(wint_t));
CRTDECL(int, iswblank(wint_t));
CRTDECL(int, iswcntrl(wint_t));
CRTDECL(int, iswctype(wint_t, wctype_t));
CRTDECL(int, iswdigit(wint_t));
CRTDECL(int, iswgraph(wint_t));
CRTDECL(int, iswlower(wint_t));
CRTDECL(int, iswprint(wint_t));
CRTDECL(int, iswpunct(wint_t));
CRTDECL(int, iswspace(wint_t));
CRTDECL(int, iswupper(wint_t));
CRTDECL(int, iswxdigit(wint_t));
CRTDECL(wint_t, towctrans(wint_t, wctrans_t));
CRTDECL(wint_t, towupper(wint_t));
CRTDECL(wint_t, towlower(wint_t));
CRTDECL(wctrans_t, wctrans(const char *));
CRTDECL(wctype_t, wctype(const char *));

CRTDECL(int, iswalpha_l (wint_t, locale_t));
CRTDECL(int, iswalnum_l (wint_t, locale_t));
CRTDECL(int, iswblank_l (wint_t, locale_t));
CRTDECL(int, iswcntrl_l (wint_t, locale_t));
CRTDECL(int, iswctype_l (wint_t, wctype_t, locale_t));
CRTDECL(int, iswdigit_l (wint_t, locale_t));
CRTDECL(int, iswgraph_l (wint_t, locale_t));
CRTDECL(int, iswlower_l (wint_t, locale_t));
CRTDECL(int, iswprint_l (wint_t, locale_t));
CRTDECL(int, iswpunct_l (wint_t, locale_t));
CRTDECL(int, iswspace_l (wint_t, locale_t));
CRTDECL(int, iswupper_l (wint_t, locale_t));
CRTDECL(int, iswxdigit_l (wint_t, locale_t));
CRTDECL(wint_t, towctrans_l (wint_t, wctrans_t, locale_t));
CRTDECL(wint_t, towupper_l (wint_t, locale_t));
CRTDECL(wint_t, towlower_l (wint_t, locale_t));
CRTDECL(wctrans_t, wctrans_l (const char *, locale_t));
CRTDECL(wctype_t, wctype_l (const char *, locale_t));
_CODE_END

#endif /* _WCTYPE_H_ */
