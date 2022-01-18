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
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS C Library - Standard Library Wide Conversion Macros
*/

/* Define posix */
#define __POSIX_VISIBLE

/* Includes */
#include <locale.h>
#include <wctype.h>

int iswprint_l(wint_t c, struct __locale_t *locale)
{
	/* Silence warning */
	_CRT_UNUSED(locale);

	/* We're using a locale-independent representation of upper/lower case
	   based on Unicode data.  Thus, the locale doesn't matter. */
	return iswprint(c);
}
