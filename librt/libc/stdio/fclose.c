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
* MollenOS C Library - FCLOSE
*/

/* Includes */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* Externs */
extern int _ffillclean(FILE * stream);

/* The _close 
 * This is ANSI C close 
 * function and works with 
 * filedescriptors */
int _close(int handle)
{
	/* Variables */
	int RetVal = 0;

	/* Syscall */
	RetVal = Syscall1(SYSCALL_VFSCLOSE, SYSCALL_PARAM(handle));

	/* Validation 
	 * we need to make sure everythis is ok */
	if (_fval(RetVal))
		return -1;
	else
		return 0;
}

/* The fclose 
 * Closes a file handle and frees 
 * resources associated */
int fclose(FILE * stream)
{
	/* Sanity input */
	if (stream == NULL
		|| stream == stdin
		|| stream == stdout
		|| stream == stderr) {
		_set_errno(EINVAL);
		return EOF;
	}

	/* Flush the file? */
	if (stream->code & _IOWRT)
		fflush(stream);

	/* Close the associated
	 * file descriptor */
	if (_close(stream->fd)) {
		free(stream);
		return EOF;
	}

	/* Cleanup the file buffer */
	_ffillclean(stream);

	/* Free the stream handle */
	free(stream);

	/* Done */
	return 0;
}