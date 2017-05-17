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
* MollenOS C Library - File Put String
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* The fputs 
 * writes a string to the given
 * stream handle */
int fputs(const char * str, FILE * stream)
{
	/* Sanity input */
	if (stream == NULL
		|| str == NULL
		|| stream == stdin) {
		_set_errno(EINVAL);
		return EOF;
	}

	/* If we are targeting stdout/stderr
	* we just redirect this call */
	if (stream == stdout
		|| stream == stderr) {
		return puts(str);
	}

	/* Use file-write */
	return fwrite(str, strlen(str), 1, stream);
}