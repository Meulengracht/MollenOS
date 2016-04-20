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
* MollenOS C Library - File Save Position
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* The fgetpos
 * Saves the file position */
int fgetpos(FILE * stream, fpos_t * pos)
{
	/* Syscall Result */
	int RetVal = 0;
	long fPos = 0;
	char Buffer[64];

	/* Sanity */
	if (stream == NULL
		|| pos == NULL) {
		_set_errno(EINVAL);
		return -1;
	}

	/* Prepare a buffer */
	memset(Buffer, 0, sizeof(Buffer));

	/* Syscall */
	RetVal = Syscall4(MOLLENOS_SYSCALL_VFSQUERY, MOLLENOS_SYSCALL_PARAM(stream),
		MOLLENOS_SYSCALL_PARAM(0),
		MOLLENOS_SYSCALL_PARAM(&Buffer[0]),
		MOLLENOS_SYSCALL_PARAM(sizeof(Buffer)));

	if (!RetVal) 
	{
		/* Now we can calculate */
		fPos = *((long*)(&Buffer[16]));

		/* Store in buffer */
		*pos = (fpos_t)fPos;

		/* Clear err and return */
		_set_errno(EOK);
		return 0;
	}

	/* Error */
	if (RetVal == -1)
		_set_errno(EINVAL);
	else if (RetVal == -2)
		_set_errno(EINVAL);
	else if (RetVal == -3)
		_set_errno(ENOENT);
	else if (RetVal == -4)
		_set_errno(ENOENT);
	else if (RetVal == -5)
		_set_errno(EACCES);
	else if (RetVal == -6)
		_set_errno(EISDIR);
	else if (RetVal == -7)
		_set_errno(EEXIST);
	else if (RetVal == -8)
		_set_errno(EIO);
	else
		_set_errno(EINVAL);

	/* Zero */
	*pos = 0;

	/* Done */
	return -1;
}