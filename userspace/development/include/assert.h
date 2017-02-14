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
* MollenOS C-Library - Assert
*/

#ifndef _CLIB_ASSERT
#define _CLIB_ASSERT

/* Includes */
#include <crtdefs.h>

/* CPP-Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* Extern */
_CRTIMP void _assert_panic(__CONST char* str);

#define __symbol2value( x ) #x
#define __symbol2string( x ) __symbol2value( x )

#undef assert

#ifdef NDEBUG
#define assert( ignore ) ( (void) 0 )
#else

#define assert( expression ) ( ( expression ) ? (void) 0 \
        : _assert_panic( "Assertion failed: " #expression \
                          ", file " __symbol2string( __FILE__ ) \
                          ", line " __symbol2string( __LINE__ ) \
                          "." ) )
#endif

/* CPP Guard */
#ifdef __cplusplus
}
#endif

#endif
