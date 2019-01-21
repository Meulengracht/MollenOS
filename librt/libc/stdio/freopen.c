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
 * MollenOS C Library - Reopen file-handle
 */

#include <ddk/file.h>
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "local.h"

/* freopen
 * Reuses stream to either open the file specified 
 * by filename or to change its access mode. */
FILE *freopen(
	_In_ __CONST char * filename, 
	_In_ __CONST char * mode, 
	_In_ FILE * stream)
{
	// Variables
	int open_flags, stream_flags;
	int fd;

	// Sanitize parameters
	if ((filename == NULL
		&& mode == NULL)
		|| stream == NULL
		|| stream == stdin
		|| stream == stdout
		|| stream == stderr) {
		_set_errno(EINVAL);
		return NULL;
	}

	// Ok, if filename is not null 
	// we must open a new file
	if (filename != NULL) {
		// Close
		close(stream->_fd);

		// Split flags
		_fflags(mode, &open_flags, &stream_flags);

		// Open and validate result
		fd = open(filename, open_flags, S_IREAD | S_IWRITE);
		if (fd == -1) {
			return NULL;
		}

		// Reinitialize handle
		if (StdioFdInitialize(stream, fd, stream_flags) != OsSuccess) {
			_set_errno(EINVAL);
		}
	}
	else {
		if (mode != NULL) {
			_fflags(mode, &open_flags, &stream_flags);
			if (SetFileOptions(stream->_fd, _fopts(open_flags), _faccess(open_flags)) != OsSuccess) {
				_set_errno(EINVAL);
			}
		}
	}

	// Done
	stream->_flag &= ~(_IOEOF | _IOERR);
	return stream;
}
