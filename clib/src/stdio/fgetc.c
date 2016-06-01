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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* The fgetc
 * Reads an character from the 
 * given input stream */
int fgetc(FILE * stream)
{
	/* Sanity */
	if (stream == NULL
		|| stream == stdout
		|| stream == stderr) {
		_set_errno(EINVAL);
		return EOF;
	}

	/* If we want to read from stdin 
	 * redirect */
	if (stream == stdin) {
		return getchar();
	}

	/* @Unimplemented */
	return 0;
}