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

    // When seeking we need to calculate the next position. Is the position within
    // the bounds of the current stream buffer? Or is it outside?
    if (__FILE_IsBuffered(file)) {
        // In all cases we must know the current position, as we must be able
        // to return the current position.
        long long base = lseeki64(file->IOD, 0, SEEK_CUR);
        long long current = base + (long long)__FILE_BytesBuffered(file);
        long long limit = base + file->BufferSize;

        // If we are seeking inside the parameters of the stream buffer,
        // then we can avoid another seek call to the VFS. If we seek outside
        // we need to maybe flush the buffer, or reset the streambuffer
        if (whence == SEEK_CUR) {
            if ((current + offset) < base) {
                // we are seeking outside
            } else if ((current + offset) >= limit) {
                // we are seeking outside
            } else {
                // we are seeking inside
            }
        } else if (whence == SEEK_SET) {

        }
    } else {
        ret = (lseeki64(file->IOD, offset, whence) == -1) ? -1 : 0;
    }

	// discard buffered input
	file->BytesAvailable = 0;
	file->Current = file->Base;

    // clear EOF
	file->Flags &= ~_IOEOF;

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
