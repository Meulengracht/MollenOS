/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 */

#include <errno.h>
#include <io.h>
#include <internal/_io.h>
#include <internal/_file.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int close(int fd)
{
	stdio_handle_t* handle;
	int             result;
	int             options = 0;

	handle = stdio_handle_get(fd);
	if (!handle) {
        _set_errno(EBADFD);
		return -1;
	}
	
	// The cases where we close is when the handle is
	// not inheritted or the handle is not persistant
	if (!(handle->wxflag & (WX_INHERITTED | WX_PERSISTANT))) {
        options |= STDIO_CLOSE_FULL;
    }

	result = handle->ops.close(handle, options);
	stdio_handle_destroy(handle, 0);
    return result;
}

int fclose(FILE *stream)
{
	int r, flag, fd;

	flockfile(stream);
	if (stream->_flag & _IOWRT) {
		fflush(stream);
	}
	flag    = stream->_flag;
    fd      = stream->_fd;

	// Flush and free all the associated buffers
	if (stream->_tmpfname != NULL) {
		free(stream->_tmpfname);
	}
	if (stream->_flag & _IOMYBUF) {
		free(stream->_base);
	}

	// Call underlying close and never unlock the file as 
	// underlying stream is closed and not safe anymore
	r = close(fd);
    funlockfile(stream);

    // Never free the standard handles, so handle that here
    if (stream != stdout && stream != stdin && stream != stderr) {
        free(stream);
    } else {
        memset(stream, 0, sizeof(FILE));
        stream->_fd = fd;
    }
	return ((r == -1) || (flag & _IOERR) ? EOF : 0);
}
