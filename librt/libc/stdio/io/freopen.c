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
 * Standard C Library
 *   - Method for reopening files with modified permissions or modified
 *     underlying file-descriptor
 */

#include <os/services/file.h>
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "../libc_io.h"

FILE* freopen(
	_In_ const char* filename, 
	_In_ const char* mode, 
	_In_ FILE*       stream)
{
	stdio_object_t* object;
	int             open_flags;
	int             stream_flags;
	int             fd;

	// Sanitize parameters
	if ((filename == NULL && mode == NULL)
		|| stream == NULL || stream == stdin
		|| stream == stdout || stream == stderr) {
		_set_errno(EINVAL);
		return NULL;
	}

	// Ok, if filename is not null we must open a new file
	if (filename != NULL) {
		close(stream->_fd);

		// Open a new file descriptor
		_fflags(mode, &open_flags, &stream_flags);
		fd = open(filename, open_flags, S_IREAD | S_IWRITE);
		if (fd == -1) {
			return NULL;
		}
		object = stdio_object_get(fd);
		stdio_object_set_buffered(object, stream, stream_flags);
	}
	else {
		if (mode != NULL) {
			_fflags(mode, &open_flags, &stream_flags);
			// TODO: support multiple types of streams
			if (SetFileOptions(stream->_fd, _fopts(open_flags), _faccess(open_flags)) != OsSuccess) {
				_set_errno(EINVAL);
			}
		}
	}
	stream->_flag &= ~(_IOEOF | _IOERR);
	return stream;
}
