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

/* The is the ANSI C file 
 * deletion method and is shared by the 'modern' */
int _unlink(__CONST char *filename)
{
	/* Sanity input */
	if (filename == NULL) {
		_set_errno(EINVAL);
		return EOF;
	}

	/* Redirect call */
	return _fval(DeleteFile(filename));
}

/* The file deletion function
 * Simply deletes the file specified
 * by the path */
int remove(__CONST char * filename)
{
	/* Simply just call this */
	return _unlink(filename);
}
