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

/* Externs */
extern int _favail(FILE * stream);

/* The ftello
 * Get the file position with off_t */
off_t ftello(FILE * stream)
{
	/* Syscall Result */
	int RetVal = 0;	
	char Buffer[64];

	/* Sanity */
	if (stream == NULL
		|| stream == stdin
		|| stream == stdout
		|| stream == stderr) {
		_set_errno(ESPIPE);
		return -1L;
	}

	/* Prepare a buffer */
	memset(Buffer, 0, sizeof(Buffer));

	/* Syscall */
	RetVal = Syscall4(MOLLENOS_SYSCALL_VFSQUERY, MOLLENOS_SYSCALL_PARAM(stream->fd),
		MOLLENOS_SYSCALL_PARAM(0),
		MOLLENOS_SYSCALL_PARAM(&Buffer[0]),
		MOLLENOS_SYSCALL_PARAM(sizeof(Buffer)));

	if (!_fval(RetVal)) 
	{
		/* Now we can calculate 
		 * but we need to remember to offset for our buffer pos */
		uint64_t fPosition = (*((off_t*)(&Buffer[16])) - _favail(stream));
		return (off_t)fPosition;
	}

	/* Done */
	return -1L;
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