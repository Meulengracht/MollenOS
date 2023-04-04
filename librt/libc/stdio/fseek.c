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
    if (!__FILE_CanSeek(file)) {
        funlockfile(file);
        errno = EACCES;
        return -1;
    }

    // If the stream is open in binary mode, the new position is exactly offset bytes measured from the
    // beginning of the file if origin is SEEK_SET, from the current file position if origin is
    // SEEK_CUR, and from the end of the file if origin is SEEK_END.
    // Binary streams are not required to support SEEK_END, in particular if additional null bytes are output.

    // If the stream is open in text mode, the only supported values for offset
    // are zero (which works with any origin) and a value returned by an earlier call
    // to ftell on a stream associated with the same file (which only works with origin of SEEK_SET).
    if (file->Flags & (_FBYTE | _FWIDE)) {
        if ((whence == SEEK_CUR || whence == SEEK_END) && offset != 0) {
            funlockfile(file);
            errno = EINVAL;
            return -1;
        }
    }

    // When seeking we need to calculate the next position. Is the position within
    // the bounds of the current stream buffer? Or is it outside?
    if (__FILE_IsBuffered(file)) {
        // In all cases we must know the current position, as we must be able
        // to return the current position.
        long long base = lseeki64(file->IOD, 0, SEEK_CUR);
        long long current = base + __FILE_BufferPosition(file);
        long long limit = base + file->BufferSize;
        if (base == -1) {
            funlockfile(file);
            return -1;
        }

        // If we are seeking inside the parameters of the stream buffer,
        // then we can avoid another seek call to the VFS. If we seek outside
        // we need to maybe flush the buffer, or reset the streambuffer
        if (whence == SEEK_CUR) {
            if ((current + offset) < base || (current + offset) >= limit) {
                ret = fflush(file);
                if (ret) {
                    funlockfile(file);
                    return ret;
                }
                ret = (lseeki64(file->IOD, offset, whence) == -1) ? -1 : 0;
            } else {
                // we are seeking inside, just update Current to point to
                // the right offset, and skip doing a seek.
                file->Current += offset;
                ret = 0;
            }
        } else if (whence == SEEK_SET) {
            if (offset < base || offset >= limit) {
                ret = fflush(file);
                if (ret) {
                    funlockfile(file);
                    return ret;
                }
                ret = (lseeki64(file->IOD, offset, whence) == -1) ? -1 : 0;
            } else {
                // we are seeking inside, just update Current to point to
                // the right offset, and skip doing a seek.
                file->Current = file->Base + (offset - base);
                ret = 0;
            }
        } else if (whence == SEEK_END) {
            // Ironically, wanting to seek the end of the file requires us to do so
            // in any case. We may however end up restoring the previous position.
            long long end = lseeki64(file->IOD, 0, SEEK_END);
            if (end == -1) {
                funlockfile(file);
                return -1;
            }

            if ((end + offset) < base || (end + offset) >= limit) {
                // We were searching outside the buffer, before we restore
                // we should now check whether a flush was required.
                if (file->Flags & _IOMOD) {
                    // The buffer had been modified, this unfortunately means
                    // we need to restore position, flush the buffer and seek again.
                    // This is our worst-case scenario. But we only do so for the write-case
                    // as read-case is just a buffer reset, we don't require any writing to
                    // the underlying IOD.
                    ret = (int)lseeki64(file->IOD, base, SEEK_SET);
                    if (ret == -1) {
                        funlockfile(file);
                        return ret;
                    }
                }

                // Flush the buffer, and then seek to the correct position, as requested.
                ret = fflush(file);
                if (ret) {
                    funlockfile(file);
                    return ret;
                }
                ret = (lseeki64(file->IOD, offset, whence) == -1) ? -1 : 0;
            } else {
                // If we are seeking inside the stream buffer, then we *must* reset the
                // underlying seek pointer, by restoring the original position
                ret = (int)lseeki64(file->IOD, base, SEEK_SET);
                if (ret == -1) {
                    funlockfile(file);
                    return ret;
                }
                file->Current += file->BytesValid + offset;
                ret = 0;
            }
        } else {
            funlockfile(file);
            errno = EINVAL;
            return -1;
        }
    } else {
        // clear the ungetc buffer
        __FILE_ResetBuffer(file);
        ret = (lseeki64(file->IOD, offset, whence) == -1) ? -1 : 0;
    }

    clearerr(file);
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
