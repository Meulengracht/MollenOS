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
 * - Closes a given file-handle and cleans up
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
#include "local.h"

/* _close
 * This is ANSI C close function and works with filedescriptors */
int _close(int fd)
{
	// Variables
	UUId_t handle;
	int result;

	// Retrieve handle
	handle = StdioFdToHandle(fd);
	if (handle == UUID_INVALID) {
		return -1;
	}

	// Call OS routine
	result = (int)CloseFile(handle);

	// Validate the result code
	if (_fval(result)) {
		return -1;
	}
	else {
		StdioFdFree(fd);
		return 0;
	}
}

/* fclose
 * Closes a file handle and frees resources associated */
int fclose(FILE *stream)
{
	// Variables
	int r, flag;

    // Flush file before anything
	if (stream->_flag & _IOWRT) {
		fflush(stream);
	}

	_lock_file(stream);
	flag = stream->_flag;

	// Flush and free associated buffers
	if (stream->_tmpfname != NULL) {
		free(stream->_tmpfname);
	}
	if (stream->_flag & _IOMYBUF) {
		free(stream->_base);
	}

	// Call underlying close
	r = _close(stream->_fd);
	_unlock_file(stream);

	// Free the stream
	free(stream);
	return ((r == -1) || (flag & _IOERR) ? EOF : 0);
}
