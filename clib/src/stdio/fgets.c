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
* MollenOS C Library - File Get String
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* The fgets
 * Reads a string (max n chars tho) 
 * from the given stream */
char *fgets(char * buf, size_t n, FILE * stream)
{
	/* Vars */
	char *p = NULL;
	int c = 0;

	/* Sanity input */
	if (stream == NULL
		|| buf == NULL
		|| n == 0) {
		_set_errno(EINVAL);
		return NULL;
	}

	/* get max bytes or upto a newline */
	for (p = buf, n--; n > 0; n--) {
		if ((c = fgetc(stream)) == EOF)
			break;
		*p++ = (char)c;
		if (c == '\n')
			break;
	}

	/* Null terminate */
	*p = 0;

	/* Sanity */
	if (p == buf || c == EOF)
		return NULL;

	/* Done! */
	return p;
}