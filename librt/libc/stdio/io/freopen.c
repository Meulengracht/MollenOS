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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Standard C Library
 *   - Method for reopening files with modified permissions or modified
 *     underlying file-descriptor
 */

#include <errno.h>
#include <internal/_io.h>
#include <internal/_ipc.h>
#include <io.h>
#include <os/mollenos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FILE* freopen(
	_In_ const char* filename, 
	_In_ const char* mode, 
	_In_ FILE*       stream)
{
	stdio_handle_t* handle;
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
		fd = open(filename, open_flags, 0755);
		if (fd == -1) {
			return NULL;
		}
		handle = stdio_handle_get(fd);
		stdio_handle_set_buffered(handle, stream, stream_flags);
	}
	else {
		if (mode != NULL) {
			struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
			oserr_t               status;
			
			handle = stdio_handle_get(stream->_fd);
			_fflags(mode, &open_flags, &stream_flags);
			// TODO: support multiple types of streams
			
			sys_file_set_access(GetGrachtClient(), &msg.base, __crt_process_id(),
                                handle->object.handle, _fopts(open_flags));
            gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
			sys_file_set_access_result(GetGrachtClient(), &msg.base, &status);
			OsErrToErrNo(status);
		}
	}
	stream->_flag &= ~(_IOEOF | _IOERR);
	return stream;
}
