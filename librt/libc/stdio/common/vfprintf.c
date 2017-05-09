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
* MollenOS C Library - variable argument file printf
*/

/* Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

int vfprintf(FILE *stream, const char *format, va_list ap)
{
	/* Variables */
	char Out[256];

	/* Sanitize input */
	if (stream == NULL
		|| stream == stdin) {
		_set_errno(EINVAL);
		return -1;
	}

	/* Sanity */
	if (stream == stdout
		|| stream == stderr)
	{
		/* Redirect the call to vprintf instead
		* as the output goes to screen instead */
		return vprintf(format, ap);
	}

	/* Reset buffer */
	memset(&Out[0], 0, sizeof(Out));

	/* Redirect to sprintf to take care of 
	 * formatting */
	vsprintf(&Out[0], format, ap);

	/* Use f-write to write 
	 * to the file */
	return fwrite(&Out[0], strlen(&Out[0]), 1, stream);
}