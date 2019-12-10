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

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <internal/_io.h>

int close(int fd)
{
	stdio_handle_t* handle;
	int             result = EOK;

	handle = stdio_handle_get(fd);
	if (!handle) {
        _set_errno(EBADFD);
		return -1;
	}
	
	// The cases where we close is when the handle is
	// not inheritted and the handle is not persistant
	if (!(handle->wxflag & (WX_INHERITTED | WX_PERSISTANT))) {
	    result = handle->ops.close(handle, 0);
	}
	stdio_handle_destroy(handle, 0);
    return result;
}

int fclose(FILE *stream)
{
	int r, flag, fd;

    assert(stream != NULL);
    
	_lock_file(stream);
	if (stream->_flag & _IOWRT) {
		fflush(stream);
	}
	flag    = stream->_flag;
    fd      = stream->_fd;

	// Flush and free associated buffers
	if (stream->_tmpfname != NULL) {
		free(stream->_tmpfname);
	}
	if (stream->_flag & _IOMYBUF) {
		free(stream->_base);
	}

	// Call underlying close and never unlock the file as 
	// underlying stream is closed and not safe anymore
	r = close(fd);
    
    // Never free the standard handles, so handle that here
    if (stream != stdout && stream != stdin && stream != stderr) {
        free(stream);
    }
    else {
        memset(stream, 0, sizeof(FILE));
        stream->_fd = fd;
    }
	return ((r == -1) || (flag & _IOERR) ? EOF : 0);
}
