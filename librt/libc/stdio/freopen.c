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
* MollenOS C Library - File Reopen
*/

/* Includes */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* Externs */
extern int _finv(FILE * stream);

/* The freopen
 * Reuses the file
 * handle to either open a new 
 * file or switch access mode */
FILE *freopen(const char * filename, const char * mode, FILE * stream)
{
	/* Variables */
	int mFlags = 0;
	int RetVal = 0;
	int ErrCode = 0;

	/* Sanity input */
	if ((filename == NULL
		&& mode == NULL)
		|| stream == NULL
		|| stream == stdin
		|| stream == stdout
		|| stream == stderr) {
		_set_errno(EINVAL);
		return NULL;
	}

	/* Invalidate the stream buffer */
	_finv(stream);

	/* Ok, if filename is not null 
	 * we must open a new file */
	if (filename != NULL) 
	{
		/* Close existing handle */
		RetVal = Syscall1(SYSCALL_VFSCLOSE, SYSCALL_PARAM(stream->fd));

		/* Convert flags */
		mFlags = fflags(mode);

		/* System call time,
		* get that file handle */
		RetVal = Syscall3(SYSCALL_VFSOPEN, SYSCALL_PARAM(filename),
			SYSCALL_PARAM(mFlags), SYSCALL_PARAM(&ErrCode));

		/* Reset */
		stream->fd = RetVal;
		stream->flags = mFlags;
		stream->code = _IOREAD | _IOFBF;

		/* Sanity */
		if (_fval(ErrCode)) {
			/* Free handle */
			_close(RetVal);
			free(stream);
			_fval(ErrCode);
			return NULL;
		}

		/* Done */
		return stream;
	}
	else
	{
		/* Switch access mode of file 
		 * --- support query */
		
		/* Convert flags */
		mFlags = fflags(mode);

		/* Syscall */
		Syscall4(SYSCALL_VFSQUERY, SYSCALL_PARAM(stream->fd), SYSCALL_PARAM(3), 
			SYSCALL_PARAM(&mFlags), SYSCALL_PARAM(sizeof(mFlags)));

		/* Store */
		stream->flags = mFlags;
		stream->code = _IOREAD | _IOFBF;

		/* clear error */
		_set_errno(EOK);

		/* Done */
		return stream;
	}
}