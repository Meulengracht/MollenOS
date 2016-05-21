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
* MollenOS C Library - File Read
*/

/* Includes */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* The read 
 * This is the ANSI C version
 * of fread */
int _read(int fd, void *buffer, unsigned int len)
{
	/* Variables */
	int RetVal = 0, ErrCode = 0;

	/* Syscall */
	RetVal = Syscall4(MOLLENOS_SYSCALL_VFSREAD, MOLLENOS_SYSCALL_PARAM(fd),
		MOLLENOS_SYSCALL_PARAM(buffer), MOLLENOS_SYSCALL_PARAM(len), MOLLENOS_SYSCALL_PARAM(&ErrCode));

	/* Sanity */
	if (_fval(ErrCode)) {
		return -1;
	}

	/* Gj */
	return RetVal;
}

/* The fread 
 * reads from a file handle */
size_t fread(void * vptr, size_t size, size_t count, FILE * stream)
{
	/* Variables */
	size_t BytesToRead = count * size;
	int RetVal = 0, ErrCode = 0;

	/* Sanity */
	if (vptr == NULL
		|| stream == NULL
		|| BytesToRead == 0)
		return 0;

	/* Syscall */
	RetVal = Syscall4(MOLLENOS_SYSCALL_VFSREAD, MOLLENOS_SYSCALL_PARAM(stream->fd),
		MOLLENOS_SYSCALL_PARAM(vptr), MOLLENOS_SYSCALL_PARAM(BytesToRead), MOLLENOS_SYSCALL_PARAM(&ErrCode));

	/* No need to check return 
	 * the syscall will set error code if any */
	BytesToRead = (size_t)RetVal;

	/* If we read 0 bytes and errcode is ok, 
	 * then we are EOF */
	if (RetVal == 0
		&& ErrCode == 0) {
		stream->code |= CLIB_FCODE_EOF;
	}

	/* Clear error */
	_set_errno(EOK);

	/* Gj */
	return BytesToRead;
}