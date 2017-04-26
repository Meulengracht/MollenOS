/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS C Library - Get file position
 */

/* Includes
 * - System */
#include <os/driver/file.h>
#include <os/syscall.h>

/* Includes 
 * - Library */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

/* Externs */
__EXTERN int _favail(FILE * stream);

/* _tell 
 * the ANSII C version of ftell */
long _tell(int fd)
{
	/* Variables */
	uint32_t Position = 0;

	/* Syscall */
	if (GetFilePosition((UUId_t)fd, &Position, NULL) != OsSuccess) {
		return -1;
	}
	else {
		return (long)Position;
	}
}

/* The ftello
 * Get the file position with off_t */
off_t ftello(FILE * stream)
{
	/* Variables */
	off_t Position = 0;
#if _FILE_OFFSET_BITS==64
	uint32_t pLo, pHi;
#endif
	
	/* Sanity */
	if (stream == NULL
		|| stream == stdin
		|| stream == stdout
		|| stream == stderr) {
		_set_errno(ESPIPE);
		return -1L;
	}

	/* Get current position */
#if _FILE_OFFSET_BITS==64
	if (GetFilePosition((UUId_t)fd, &pLo, &pHi) != OsSuccess) {
		return -1L;
	}
	else {
		Position = (pHi << 32) | pLo;
	}
#else
	Position = (off_t)_tell(stream->fd);

	/* Sanity */
	if (Position == -1) {
		return -1L;
	}
#endif

	/* Adjust for buffering */
	if (_favail(stream) != 0) {
		Position -= _favail(stream);
	}

	/* Done */
	return Position;
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
