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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Standard C Library
 * - Sets the position indicator associated with the stream to a new position.
 */

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <internal/_io.h>

long long lseeki64(
	_In_ int        fd, 
	_In_ long long  offset, 
	_In_ int        whence)
{
    stdio_handle_t* handle   = stdio_handle_get(fd);
	long long       position = 0;

    if (handle == NULL) {
        _set_errno(EBADFD);
        return -1;
    }

	if (whence < 0 || whence > 2) {
		_set_errno(EINVAL);
		return -1;
	}
	
	if (handle->ops.seek(handle, whence, offset, &position)) {
		return -1;
	}
	
	// clear out eof after seeks
	handle->wxflag &= ~(WX_ATEOF|WX_READEOF);
	return position;
}

long lseek(
	_In_ int  fd,
	_In_ long offset,
	_In_ int  whence)
{
	return (long)lseeki64(fd, offset, whence);
}

/* fseeki64
 * Sets the position indicator associated with the stream to a new position. */
int fseeki64(
	_In_ FILE*      file, 
	_In_ long long  offset, 
	_In_ int        whence)
{
	int ret;

	// Lock access to stream
	_lock_file(file);
	if (file->_flag & _IOWRT) {
        io_buffer_flush(file);
	}

	// Adjust for current position
	if (whence == SEEK_CUR && (file->_flag & _IOREAD)) {
		whence = SEEK_SET;
		offset += ftelli64(file);
	}

	// Discard buffered input
	file->_cnt = 0;
	file->_ptr = file->_base;

	// Reset direction of i/o
	if (file->_flag & _IORW) {
		file->_flag &= ~(_IOREAD | _IOWRT);
	}
	file->_flag &= ~_IOEOF;
	ret = (lseeki64(file->_fd, offset, whence) == -1) ? -1 : 0;

	_unlock_file(file);
	return ret;
}

/* fseek
 * Sets the position indicator associated with the stream to a new position. */
int fseek(
	_In_ FILE *stream,
	_In_ long int offset,
	_In_ int origin)
{
	return fseeki64(stream, offset, origin);
}

/* fseeko
 * Sets the position indicator associated with the stream to a new position. */
int fseeko(
	_In_ FILE *stream,
	_In_ off_t offset,
	_In_ int origin)
{
	return fseeki64(stream, offset, origin);
}
