/* MollenOS
 *
 * Copyright 2011 - 2018, Philip Meulengracht
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
 * MollenOS Standard C - Assert Implementation
 */

#ifndef __STDC_ASSERT__
#define __STDC_ASSERT__

#include <crtdefs.h>

_CODE_BEGIN
CRTDECL(void, _assert_panic(const char* str));

#define __symbol2value( x ) #x
#define __symbol2string( x ) __symbol2value( x )

#undef assert
#ifdef NDEBUG
#define assert( ignore ) ( (void) 0 )
#else
#if __STDC_VERSION__ >= 199901L
#define assert( expression ) ( ( expression ) ? (void) 0 \
        : _assert_panic( "Assertion failed: " #expression \
                          ", function ", __func__, \
                          ", file " __FILE__ \
                          ", line " __symbol2string( __LINE__ ) \
                          ".\n" ) )
#else
#define assert( expression ) ( ( expression ) ? (void) 0 \
        : _assert_panic( "Assertion failed: " #expression \
                          ", file " __FILE__ \
                          ", line " __symbol2string( __LINE__ ) \
                          ".\n" ) )
#endif
_CODE_END

#if defined __USE_ISOC11 && !defined __cplusplus
# undef static_assert
# define static_assert _Static_assert
#endif

#endif //!__STDC_ASSERT__
