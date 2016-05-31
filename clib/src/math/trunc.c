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
* MollenOS - Truncate functions 
*/

/* Includes */
#include <math.h>
#include <float.h>

/* To compute the integer part of X, sum a big enough
 * integer so that the precision of the double 
 * number is exactly 1. */
double trunc(double x)
{
	return floor(x);
}

/* To compute the integer part of X, sum a big enough
 * integer so that the precision of the floating point
 * number is exactly 1. */
float truncf(float x)
{
	return floorf(x);
}