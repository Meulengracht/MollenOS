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
 * MollenOS C Library - OS-File Utilities
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

/* The faccess 
 * Converts the C-mode flags into our access flags */
Flags_t faccess(__CONST char * mode)
{
	/* Variables */
	Flags_t mFlags = 0;
	int PlusConsumed = 1;

	/* Read modes first */
	if (strchr(mode, 'r') != NULL) {
		mFlags |= __FILE_READ_ACCESS;
		if (strchr(mode, '+') != NULL) {
			mFlags |= __FILE_WRITE_ACCESS;
			PlusConsumed = 1;
		}
	}

	/* Write modes */
	if (strchr(mode, 'w') != NULL) {
		mFlags |= __FILE_WRITE_ACCESS;
		if (!PlusConsumed
			&& strchr(mode, '+') != NULL) {
			mFlags |= __FILE_READ_ACCESS;
			PlusConsumed = 1;
		}
	}

	/* Append modes */
	if (strchr(mode, 'a') != NULL) {
		mFlags |= __FILE_WRITE_ACCESS;
		if (!PlusConsumed
			&& strchr(mode, '+') != NULL) {
			mFlags |= __FILE_READ_ACCESS;
			PlusConsumed = 1;
		}
	}

	/* Done */
	return mFlags;
}

/* The faccess 
 * Converts the C-mode flags into our option flags */
Flags_t fopts(__CONST char * mode)
{
	/* Variables */
	Flags_t mFlags = 0;
	int PlusConsumed = 1;

	/* Write modes */
	if (strchr(mode, 'w') != NULL) {
		mFlags |= __FILE_CREATE | __FILE_TRUNCATE;
		if (!PlusConsumed
			&& strchr(mode, '+') != NULL) {
			PlusConsumed = 1;
		}
		if (strchr(mode, 'x') != NULL) {
			mFlags |= __FILE_FAILONEXIST;
		}
	}

	/* Append modes */
	if (strchr(mode, 'a') != NULL) {
		mFlags |= __FILE_APPEND | __FILE_CREATE;
		if (!PlusConsumed
			&& strchr(mode, '+') != NULL) {
			PlusConsumed = 1;
		}
	}

	/* Specials */
	if (strchr(mode, 'b') != NULL) {
		mFlags |= __FILE_BINARY;
	}

	/* Done */
	return mFlags;
}

/* The _faccess
 * Converts the ANSI-C-mode
 * flags into our access flags */
Flags_t _faccess(int oflags)
{
	/* Variables */
	Flags_t mFlags = __FILE_READ_ACCESS;

	/* First we take care of read/write */
	if (oflags & _O_WRONLY)
		mFlags = __FILE_WRITE_ACCESS;
	if (oflags & _O_RDWR)
		mFlags |= __FILE_READ_ACCESS | __FILE_WRITE_ACCESS;

	/* Done! */
	return mFlags;
}

/* The _fopts
 * Converts the ANSI-C-mode
 * flags into our option flags */
Flags_t _fopts(int oflags)
{
	/* Variables */
	Flags_t mFlags = 0;

	/* Now we take care of specials */
	if (oflags & _O_CREAT)
		mFlags |= __FILE_CREATE;
	if (oflags & _O_TRUNC)
		mFlags |= __FILE_TRUNCATE;
	if (oflags & _O_EXCL)
		mFlags |= __FILE_FAILONEXIST;

	/* Now we take care of the
	* different data modes */
	if (oflags & _O_BINARY)
		mFlags |= __FILE_BINARY;

	/* Done */
	return mFlags;
}

/* The _fval 
 * Validates a vfs response 
 * and converts it to errno */
int _fval(int ocode)
{
	/* Switch error */
	if (ocode == 0)
		_set_errno(EOK);
	else if (ocode == 1)
		_set_errno(EINVAL);
	else if (ocode == 2)
		_set_errno(EINVAL);
	else if (ocode == 3)
		_set_errno(ENOENT);
	else if (ocode == 4)
		_set_errno(ENOENT);
	else if (ocode == 5)
		_set_errno(EACCES);
	else if (ocode == 6)
		_set_errno(EISDIR);
	else if (ocode == 7)
		_set_errno(EEXIST);
	else if (ocode == 8)
		_set_errno(EIO);
	else
		_set_errno(EINVAL);

	/* Return the code */
	return errno;
}
