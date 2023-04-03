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
#include <io.h>
#include <internal/_file.h>
#include <internal/_io.h>
#include <limits.h>
#include <os/mollenos.h>
#include <stdio.h>
#include <string.h>

static int
__prewrite_buffer(
        _In_ FILE*       stream,
        _In_ const void* buffer,
        _In_ int         count)
{
    int bytesAvailable = stream->BufferSize - __FILE_BufferPosition(stream);
    int bytesToWrite = MIN(bytesAvailable, count);
    memcpy(stream->Current, buffer, bytesToWrite);

    stream->Current += bytesToWrite;
    __FILE_UpdateBytesValid(stream);

    stream->Flags |= _IOMOD;
    return bytesToWrite;
}

size_t fwrite(const void* vptr, size_t size, size_t count, FILE* stream)
{
	size_t wrcnt   = size * count;
	int    written = 0;
    TRACE("fwrite(count=%u)", wrcnt);

	if (vptr == NULL || stream == NULL) {
		_set_errno(EINVAL);
		return 0;
	}

    // If zero bytes are requested, simply return. An error action is not
    // expected in this case.
    if (wrcnt == 0) {
        return 0;
    }

	flockfile(stream);
    if (!__FILE_CanWrite(stream)) {
        stream->Flags |= _IOERR;
        funlockfile(stream);
        errno = EACCES;
        return EOF;
    }

    // Ensure a buffer is present if possible. We need it before reading
    io_buffer_ensure(stream);

    // Fill the buffer before continuing, and flush if neccessary.
    if (__FILE_IsBuffered(stream)) {
        int bytesAvailable = stream->BufferSize - __FILE_BufferPosition(stream);
        int bytesWritten = __prewrite_buffer(stream, vptr, (int)wrcnt);
        if (bytesWritten) {
            TRACE("fwrite: wrote %i bytes to internal buffer", bytesWritten);
            written += bytesWritten;
            wrcnt -= bytesWritten;
            vptr = (const char*)vptr + bytesWritten;
        }

        // Should we flush?
        if (bytesWritten == bytesAvailable) {
            TRACE("fwrite: flushing write buffer");
            if (fflush(stream)) {
                funlockfile(stream);
                return -1;
            }
        }
    }

    // Because of pre-fill we can simplify the loop a bit. We can now make the assumption that
    // the buffer will be empty when entering the loop.
    while (wrcnt) {
        int bytesWritten;
        int chunkSize = MIN(wrcnt, INT_MAX);
        TRACE("fwrite: %u bytes left", wrcnt);

        // We cannot perform writing to the underlying IOD if this is
        // a strange resource
        if (__FILE_IsStrange(stream)) {
            stream->Flags |= _IOERR;
            break;
        }

        // There are now 2 cases, either the number of bytes to write are less
        // than what can fit in the buffer space, which means we just fill the buffer
        // and move on, or we need to write more than can fit.
        if (__FILE_IsBuffered(stream) && chunkSize < stream->BufferSize) {
            bytesWritten = __prewrite_buffer(stream, vptr, chunkSize);
        } else {
            TRACE("fwrite: writing %u bytes directly", chunkSize);
            bytesWritten = write(stream->IOD, vptr, chunkSize);
        }
        TRACE("fwrite: wrote %i bytes", bytesWritten);

        if (bytesWritten == -1) {
            stream->Flags |= _IOERR;
            break;
        } else if (bytesWritten > 0) {
            stream->Flags |= _IOMOD;
            written += bytesWritten;
            wrcnt -= bytesWritten;
            vptr = (const char*)vptr + bytesWritten;
        }
	}
	funlockfile(stream);
	return written / size;
}
