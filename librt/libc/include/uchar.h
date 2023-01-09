/**
 * Copyright 2011 - 2017, Philip Meulengracht
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
 *
 *
 * MollenOS C11-Support UTF16/32 Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __STDC_UCHAR__
#define __STDC_UCHAR__

// list of types exposed through uchar.h
#define __need_size_t

#include <crtdefs.h>
#include <stddef.h>
#include <wchar.h>

#if defined __GNUC__ && !defined __USE_ISOCXX11
/* Define the 16-bit and 32-bit character types.  Use the information
   provided by the compiler.  */
# if !defined __CHAR16_TYPE__ || !defined __CHAR32_TYPE__
#  if defined __STDC_VERSION__ && __STDC_VERSION__ < 201000L
#   error "<uchar.h> requires ISO C11 mode"
#  else
#   error "definitions of __CHAR16_TYPE__ and/or __CHAR32_TYPE__ missing"
#  endif
# endif
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;
#endif

_CODE_BEGIN
/* Write char16_t representation of multibyte character pointed
   to by S to PC16.  */
extern size_t mbrtoc16 (char16_t *__restrict __pc16,
			const char *__restrict __s, size_t __n,
			mbstate_t *__restrict __p);

/* Write multibyte representation of char16_t C16 to S.  */
extern size_t c16rtomb (char *__restrict __s, char16_t __c16,
			mbstate_t *__restrict __ps);

/* Write char32_t representation of multibyte character pointed
   to by S to PC32.  */
extern size_t mbrtoc32 (char32_t *__restrict __pc32,
			const char *__restrict __s, size_t __n,
			mbstate_t *__restrict __p);

/* Write multibyte representation of char32_t C32 to S.  */
extern size_t c32rtomb (char *__restrict __s, char32_t __c32,
			mbstate_t *__restrict __ps);

_CODE_END

#endif //!__STDC_UCHAR__
