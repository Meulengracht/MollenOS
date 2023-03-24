/**
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
 */

#include "errno.h"
#include "io.h"
#include "internal/_file.h"
#include "internal/_io.h"
#include "stdio.h"

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

    if (!handle->Ops->seek) {
        _set_errno(ENOTSUP);
        return -1;
    }
	
	if (handle->Ops->seek(handle, whence, offset, &position)) {
		return -1;
	}
	return position;
}

long lseek(
	_In_ int  fd,
	_In_ long offset,
	_In_ int  whence)
{
	return (long)lseeki64(fd, offset, whence);
}

int fseeki64(
	_In_ FILE*      file, 
	_In_ long long  offset, 
	_In_ int        whence)
{
	int ret;

	// Lock access to stream
	flockfile(file);

    // Ensure that we support the stream mode on this stream
    if (!__FILE_StreamModeSupported(file, __STREAMMODE_SEEK)) {
        funlockfile(file);
        errno = EACCES;
        return -1;
    }

    // Ensure that the stream is flushed on seek operations.
    if (__FILE_ShouldFlush(file, __STREAMMODE_SEEK)) {
        ret = fflush(file);
        if (ret) {
            funlockfile(file);
            return ret;
        }
    }

    // update stream mode
    __FILE_SetStreamMode(file, __STREAMMODE_SEEK);

	// discard buffered input
	file->_cnt = 0;
	file->_ptr = file->_base;

    // clear EOF
	file->Flags &= ~_IOEOF;

	ret = (lseeki64(file->IOD, offset, whence) == -1) ? -1 : 0;
	funlockfile(file);
	return ret;
}

int fseek(
	_In_ FILE*    stream,
	_In_ long int offset,
	_In_ int      origin)
{
	return fseeki64(stream, offset, origin);
}

int fseeko(
	_In_ FILE* stream,
	_In_ off_t offset,
	_In_ int   origin)
{
	return fseeki64(stream, offset, origin);
}
