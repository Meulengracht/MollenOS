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

#define __need_minmax
//#define __TRACE
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
    if (bytesToCopy == 0) {
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

    // If zero bytes are requested, simply return. An error action is not
    // expected in this case.
	if (rcnt == 0) {
		return 0;
	}

    // Should not happen
    flockfile(stream);

    // Ensure that this mode is supported
    if (!__FILE_StreamModeSupported(stream, __STREAMMODE_READ)) {
        errno = EACCES;
        return EOF;
    }
    __FILE_SetStreamMode(stream, __STREAMMODE_READ);

    // Try to ensure a buffer is present if possible.
    io_buffer_ensure(stream);

    // preread from the internal buffer
    bytesRead = __preread_buffer(stream, vptr, (int)rcnt);
    if (bytesRead) {
        cread += bytesRead;
        rcnt -= bytesRead;
        vptr = (char*)vptr + bytesRead;
    }

	// Keep reading untill all requested bytes are read, or EOF
	while (rcnt > 0) {
        // We cannot perform reading from the underlying IOD if this is
        // a strange resource
        if (__FILE_IsStrange(stream)) {
            stream->Flags |= _IOERR;
            break;
        }

		// if buffer is empty and the data fits into the buffer, then we fill that instead
		if (__FILE_IsBuffered(stream) && rcnt < stream->_bufsiz) {
			stream->_cnt = read(stream->IOD, stream->_base, stream->_bufsiz);
			stream->_ptr = stream->_base;
            bytesRead = MIN(stream->_cnt, rcnt);

			/* If the buffer fill reaches eof but fread wouldn't, clear eof. */
			if (bytesRead > 0 && bytesRead < stream->_cnt) {
				stream->Flags &= ~_IOEOF;
			}
			
			if (bytesRead > 0) {
				memcpy(vptr, stream->_ptr, bytesRead);
				stream->_cnt -= bytesRead;
				stream->_ptr += bytesRead;
			}
		} else if (rcnt > INT_MAX) {
            bytesRead = read(stream->IOD, vptr, INT_MAX);
		} else {
            bytesRead = read(stream->IOD, vptr, rcnt);
		}

		pread += bytesRead;
		rcnt -= bytesRead;
		vptr = (char *)vptr + bytesRead;

		// Check for EOF condition
		// also for error conditions
		if (bytesRead == 0) {
			stream->Flags |= _IOEOF;
		} else if (bytesRead == -1) {
			stream->Flags |= _IOERR;
		}

		// Break if bytes read is 0 or below
		if (bytesRead < 1) {
			break;
		}
	}
    funlockfile(stream);
	return ((cread + pread) / size);
}
