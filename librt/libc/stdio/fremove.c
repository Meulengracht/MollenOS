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
 * MollenOS - C Standard Library
 * - Deletes the file specified by the path
 */

#include <os/file.h>
#include <os/syscall.h>

#include <io.h>
#include <stdio.h>
#include <errno.h>

/* unlink
 * The is the ANSI C file 
 * deletion method and is shared by the 'modern' */
int unlink(
	_In_ const char *path) {
	if (path == NULL) {
		_set_errno(EINVAL);
		return EOF;
	}
	return _fval(DeletePath(path, __FILE_DELETE_RECURSIVE));
}

/* remove
 * Deletes either a file or a directory specified by the path. */
int remove(const char * filename) {
	return unlink(filename);
}
