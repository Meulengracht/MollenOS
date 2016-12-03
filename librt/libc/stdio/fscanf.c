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
* MollenOS C Library - File Get Character
*/

/* Includes */
#include <stdio.h>

/* The fscanf 
 * scans a file in a specific line format */
int fscanf(FILE *stream, const char *format, ...)
{
	/* Variables for trans */
	int RetVal;
	va_list ap;

	/* Start VA */
	va_start(ap, format);
	RetVal = __svfscanf(stream, format, ap);
	va_end(ap);

	/* Done */
	return RetVal;
}

/* The vscanf 
 * same as above but the va_list is 
 * set */
int vfscanf(FILE *stream, const char *format, va_list arg) {
	return __svfscanf(stream, format, arg);
}