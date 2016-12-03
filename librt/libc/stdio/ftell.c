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
* MollenOS C Library - File Tell Position
*/

/* Includes */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>
#include <os/MollenOS.h>

/* Externs */
extern int _favail(FILE * stream);

/* _tell 
 * the ANSII C version of ftell */
long _tell(int fd)
{
	/* Variables */
	char Buffer[64];
	int RetVal = 0;

	/* Prepare a buffer */
	memset(Buffer, 0, sizeof(Buffer));

	/* Syscall */
	RetVal = Syscall4(MOLLENOS_SYSCALL_VFSQUERY, MOLLENOS_SYSCALL_PARAM(fd),
		MOLLENOS_SYSCALL_PARAM(0),
		MOLLENOS_SYSCALL_PARAM(&Buffer[0]),
		MOLLENOS_SYSCALL_PARAM(sizeof(Buffer)));

	/* Sanity the result */
	if (!_fval(RetVal)) {
		return *((long*)(&Buffer[16]));
	}
	else
		return -1;
}

/* The ftello
 * Get the file position with off_t */
off_t ftello(FILE * stream)
{
	/* Variables */
	long Position = 0;	
	
	/* Sanity */
	if (stream == NULL
		|| stream == stdin
		|| stream == stdout
		|| stream == stderr) {
		_set_errno(ESPIPE);
		return -1L;
	}

	/* Get current position */
	Position = _tell(stream->fd);
	
	/* Sanity */
	if (Position == -1)
		return -1L;

	/* Adjust for buffering */
	if (_favail(stream) != 0) {
		Position -= _favail(stream);
	}

	/* Done */
	return (off_t)Position;
}

/* The ftell
 * Get the file position */
long ftell(FILE * stream)
{ 
	/* Vars */
	off_t rOffset;

	/* Get offset */
	rOffset = ftello(stream);

	/* Sanity offset */
	if ((long)rOffset != rOffset) {
		_set_errno(EOVERFLOW);
		return -1L;
	}

	/* Done! */
	return rOffset;
}
