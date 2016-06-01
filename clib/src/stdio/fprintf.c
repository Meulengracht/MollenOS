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
* MollenOS C Library - File printf 
*/

/* Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

/* This shall not be included for the kernel c library */
#ifndef LIBC_KERNEL

int fprintf(FILE * stream, const char * format, ...)
{
	/* Variables */
	va_list Arguments;
	int RetVal;
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
		va_start(Arguments, format);
		RetVal = vprintf(format, Arguments);
		va_end(Arguments);

		/* Done! */
		return RetVal;
	}

	/* Reset buffer */
	memset(&Out[0], 0, sizeof(Out));

	/* Build buffer */
	va_start(Arguments, format);
	RetVal = vsprintf(&Out[0], format, Arguments);
	va_end(Arguments);

	/* Write to stream */
	return fwrite(&Out[0], strlen(&Out[0]), 1, stream);
}

#endif // !LIBC_KERNEL