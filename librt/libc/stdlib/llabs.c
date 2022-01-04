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
* MollenOS C Library - Absolute Value
*/

/* Includes */
#include <stdlib.h>

/* Disable warning + pragma */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(disable: 4164)
#pragma function(llabs)
#endif

/* Absolutes the given integer value
* to a positive value */
long long llabs(long long j)
{
	return j < 0 ? -j : j;
}
