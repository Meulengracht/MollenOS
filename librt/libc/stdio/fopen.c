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
 * MollenOS C Library - File Opening & File Creation
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

/* _open_shared 
 * Shared open function 
 * between the handles */
int _open_shared(__CONST char *file, Flags_t Options, Flags_t Access)
{
	/* Variables */
	FileSystemCode_t ErrCode = 0;
	int RetVal = 0;

	/* System call time, get that file handle */
	RetVal = (int)OpenFile(file, Options, Access, &ErrCode);

	/* Ok, so we validate the error-code
	 * If it's wrong we have to close the file
	 * handle again, it almost always opens */
	if (_fval(ErrCode)) {
		_close(RetVal);
		_fval(ErrCode);
		return -1;
	}
	else {
		return RetVal;
	}
}

/* _open
 * This is the old ANSI C version, and is here for
 * backwards compatability */
int _open(__CONST char *file, int oflags, int pmode)
{
	/* Sanity input */
	if (file == NULL) {
		_set_errno(EINVAL);
		return -1;
	}

	/* Silence warning */
	_CRT_UNUSED(pmode);

	/* Deep call! */
	return _open_shared(file, _fopts(oflags), _faccess(oflags));
}

/* Information 
"r"	read: Open file for input operations. The file must exist.
"w"	write: Create an empty file for output operations. If a file with the same name already exists, its contents are discarded and the file is treated as a new empty file.
"a"	append: Open file for output at the end of a file. Output operations always write data at the end of the file, expanding it. 
	Repositioning operations (fseek, fsetpos, rewind) are ignored. The file is created if it does not exist.
"r+" read/update: Open a file for update (both for input and output). The file must exist.
"w+" write/update: Create an empty file and open it for update (both for input and output). If a file with the same name already exists its contents are discarded and the file is treated as a new empty file.
"a+" append/update: Open a file for update (both for input and output) with all output operations writing data at the end of the file. 
	 Repositioning operations (fseek, fsetpos, rewind) affects the next input operations, but output operations move the position back to the end of file. The file is created if it does not exist.
*/

FILE *fdopen(int fd, __CONST char *mode)
{
	/* First of all, sanity the fd */
	if (fd < 0)
		return NULL;

	/* Allocate a new file instance 
	 * and reset the structure */
	FILE *stream = (FILE*)malloc(sizeof(FILE));
	memset(stream, 0, sizeof(FILE));

	/* Initialize instance */
	stream->fd = fd;
	stream->code = _IOREAD | _IOFBF;

	/* Do we need to change access mode ? */
	if (mode != NULL) {
		/* Convert flags */
		int mFlags = fflags(mode);

		/* Syscall */
		Syscall4(SYSCALL_VFSQUERY, SYSCALL_PARAM(fd), SYSCALL_PARAM(3), 
			SYSCALL_PARAM(&mFlags), SYSCALL_PARAM(sizeof(mFlags)));

		/* Store */
		stream->flags = mFlags;
	}
	else {
		/* Convert flags */
		int mFlags = 0;

		/* Syscall */
		Syscall4(SYSCALL_VFSQUERY, SYSCALL_PARAM(fd), SYSCALL_PARAM(2),
			SYSCALL_PARAM(&mFlags), SYSCALL_PARAM(sizeof(mFlags)));

		/* Store */
		stream->flags = mFlags;
	}

	/* Set code */
	_set_errno(EOK);

	/* done! */
	return stream;
}

/* The fopen */
FILE *fopen(__CONST char * filename, __CONST char * mode)
{
	/* Variables */
	int RetVal = 0;

	/* Sanity input */
	if (filename == NULL || mode == NULL) {
		_set_errno(EINVAL);
		return NULL;
	}

	/* Use the shared open */
	RetVal = _open_shared(filename, fopts(mode), faccess(mode));

	/* Sanity */
	if (RetVal == -1) {
		return NULL;
	}

	/* Just return fdopen */
	return fdopen(RetVal, NULL);
}
