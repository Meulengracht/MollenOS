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
* MollenOS C Library - FOPEN
*/

/* Includes */
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <os/Syscall.h>

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

/* The fopen */
FILE *fopen(const char * filename, const char * mode)
{
	/* Variables */
	int mFlags = 0;
	int RetVal = 0;

	/* Sanity input */
	if (filename == NULL
		|| mode == NULL) {
		_set_errno(EINVAL);
		return NULL;
	}

	/* Convert flags */
	mFlags = fflags(mode);

	/* Now allocate a structure */
	FILE *fHandle = (FILE*)malloc(sizeof(FILE));
	fHandle->_handle = NULL;
	fHandle->code = 0;
	fHandle->status = 0;
	fHandle->flags = mFlags;

	/* Syscall */
	RetVal = Syscall3(MOLLENOS_SYSCALL_VFSOPEN, MOLLENOS_SYSCALL_PARAM(filename),
		MOLLENOS_SYSCALL_PARAM(fHandle), MOLLENOS_SYSCALL_PARAM(mFlags));

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

		/* Free handle */
		free(fHandle);
		return NULL;
	}
	
	/* Clear Error */
	_set_errno(EOK);
	
	/* Done */
	return fHandle;
}