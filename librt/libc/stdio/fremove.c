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
* MollenOS C Library - File Deletion
*/

/* Includes */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

/* The is the ANSI C file 
 * deletion method and is shared
 * by the 'modern' */
int _unlink(const char *filename)
{
	/* Sanity input */
	if (filename == NULL) {
		_set_errno(EINVAL);
		return EOF;
	}

	/* Make sure file exists
	* and we have access */
	int fd = _open(filename, _O_RDWR, 0);
	int RetVal = 0;

	/* Sanity */
	if (fd == -1)
		return errno;

	/* Syscall */
	RetVal = Syscall1(MOLLENOS_SYSCALL_VFSDELETE, MOLLENOS_SYSCALL_PARAM(fd));

	/* Close handle */
	_close(fd);

	/* Validate */
	return _fval(RetVal);
}

/* The file deletion function
 * Simply deletes the file specified
 * by the path */
int remove(const char * filename)
{
	/* Simply just call this */
	return _unlink(filename);
}