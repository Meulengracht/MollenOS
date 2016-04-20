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
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* The fclose 
 * Closes a file handle and frees 
 * resources associated */
int fclose(FILE * stream)
{
	/* Variables */
	int RetVal = 0;

	/* Sanity input */
	if (stream == NULL) {
		_set_errno(EINVAL);
		return EOF;
	}

	/* Syscall */
	RetVal = Syscall1(MOLLENOS_SYSCALL_VFSCLOSE, MOLLENOS_SYSCALL_PARAM(stream));

	/* Sanity */
	if (RetVal) {
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

		/* Return */
		return EOF;
	}
	
	/* Clear error */
	_set_errno(EOK);

	/* Done */
	return 0;
}