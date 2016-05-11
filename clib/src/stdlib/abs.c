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
* MollenOS C Library - Absolute Value
*/

/* Includes */
#include <stdlib.h>
#include <math.h>

/* Disable warning + pragma */
#ifdef _MSC_VER
#pragma warning(disable: 4164)
#pragma function(abs)
#endif

/* Absolutes the given int to a positive 
 * value */
int abs(int j)
{
	return j < 0 ? -j : j;
}

/* Complex absolute */
double _cabs(struct _complex z)
{
	return sqrt(z.x*z.x + z.y*z.y);
}

#if (_MOLLENOS >= 0x100) && \
	(defined(__x86_64) || defined(_M_AMD64) || \
	defined (__ia64__) || defined (_M_IA64))

_Check_return_
float
__cdecl
fabsf(
_In_ float x)
{
	return (float)fabs((double)x);
}

#endif