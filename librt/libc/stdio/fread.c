/**
 * Copyright 2023, Philip Meulengracht
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

#define __need_minmax
#define __TRACE
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_file.h>
#include <internal/_io.h>
#include <io.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

static int
__preread_buffer(
        _In_ FILE*  stream,
        _In_ void*  buffer,
        _In_ int    count)
{
    int bytesToCopy = MIN(count, stream->_cnt);
    if (bytesToCopy <= 0) {
        return 0;
    }

    memcpy(buffer, stream->_ptr, bytesToCopy);

    stream->_cnt -= bytesToCopy;
    stream->_ptr += bytesToCopy;
    return bytesToCopy;
}

size_t
fread(void *vptr, size_t size, size_t count, FILE *stream)
{
	size_t rcnt = size * count;
	size_t cread = 0;
	size_t pread = 0;
    int    bytesRead;
    TRACE("fread(count=%u)", rcnt);

    if (vptr == NULL || stream == NULL) {
        _set_errno(EINVAL);
        return 0;
    }

    // If zero bytes are requested, simply return. An error action is not
    // expected in this case.
	if (rcnt == 0) {
		return 0;
	}

    // Should not happen
    flockfile(stream);

    // Ensure that this mode is supported
    if (!__FILE_StreamModeSupported(stream, __STREAMMODE_READ)) {
        ERROR("fread: not supported");
        stream->Flags |= _IOERR;
        funlockfile(stream);
        errno = EACCES;
        return 0;
    }

    // Ensure a buffer is present if possible. We need it before reading
    io_buffer_ensure(stream);

    // Should we flush the current buffer? If the last operation was a write
    // we must flush
    if (__FILE_ShouldFlush(stream, __STREAMMODE_READ)) {
        int ret = fflush(stream);
        if (ret) {
            ERROR("fread: failed to flush");
            stream->Flags |= _IOERR;
            funlockfile(stream);
            return 0;
        }
    }

    // We are now doing a read operation
    __FILE_SetStreamMode(stream, __STREAMMODE_READ);

    // preread from the internal buffer
    bytesRead = __preread_buffer(stream, vptr, (int)rcnt);
    if (bytesRead) {
        TRACE("fread: read %i bytes from internal buffer", bytesRead);
        cread += bytesRead;
        rcnt -= bytesRead;
        vptr = (char*)vptr + bytesRead;
    }

	// Keep reading untill all requested bytes are read, or EOF
	while (rcnt > 0) {
        TRACE("fread: %u bytes left", rcnt);

        // We cannot perform reading from the underlying IOD if this is
        // a strange resource
        if (__FILE_IsStrange(stream)) {
            ERROR("fread: cannot read from underlying iod with strange streams");
            stream->Flags |= _IOERR;
            break;
        }

		// If buffer is empty and the data fits into the buffer, then we fill that instead
		if (__FILE_IsBuffered(stream) && rcnt < stream->_bufsiz) {
            TRACE("fread: filling read buffer of size %i", stream->_bufsiz);
			stream->_cnt = read(stream->IOD, stream->_base, stream->_bufsiz);
			stream->_ptr = stream->_base;
            bytesRead = MIN(stream->_cnt, rcnt);

			if (bytesRead > 0) {
				memcpy(vptr, stream->_ptr, bytesRead);
				stream->_cnt -= bytesRead;
				stream->_ptr += bytesRead;
			}
        } else if (rcnt >= INT_MAX) {
            bytesRead = read(stream->IOD, vptr, INT_MAX);
		} else {
            TRACE("fread: filling buffer directly of size %i", rcnt);
            bytesRead = read(stream->IOD, vptr, rcnt);
		}
        TRACE("fread: read %i bytes", bytesRead);

		// Check for EOF condition
		// also for error conditions
		if (bytesRead == 0) {
			stream->Flags |= _IOEOF;
            break;
		} else if (bytesRead == -1) {
			stream->Flags |= _IOERR;
            break;
		} else {
            pread += bytesRead;
            rcnt -= bytesRead;
            vptr = (char *)vptr + bytesRead;
        }
	}
    funlockfile(stream);
	return ((cread + pread) / size);
}
