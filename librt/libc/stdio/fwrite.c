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
#include <os/mollenos.h>
#include <stdio.h>
#include <string.h>

static int
__fill_buffer(
        _In_ FILE*       stream,
        _In_ const void* buffer,
        _In_ int         count)
{
    int bytesToCopy = MIN(stream->_cnt, count);
    if (bytesToCopy <= 0) {
        return 0;
    }

    memcpy(stream->_ptr, buffer, count);
    stream->_cnt -= count;
    stream->_ptr += count;
    return count;
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

    // Ensure that this mode is supported
    if (!__FILE_StreamModeSupported(stream, __STREAMMODE_WRITE)) {
        ERROR("fwrite: not supported");
        stream->Flags |= _IOERR;
        funlockfile(stream);
        errno = EACCES;
        return EOF;
    }

    // Ensure a buffer is present if possible. We need it before reading
    io_buffer_ensure(stream);

    // Should we flush the current buffer? If the last operation was a write
    // we must flush
    if (__FILE_ShouldFlush(stream, __STREAMMODE_WRITE)) {
        int ret = fflush(stream);
        if (ret) {
            ERROR("fwrite: failed to flush");
            funlockfile(stream);
            return ret;
        }
    }

    // We are now doing a read operation
    __FILE_SetStreamMode(stream, __STREAMMODE_WRITE);

    while (wrcnt) {
        int bytesWritten;
        TRACE("fwrite: %u bytes left", wrcnt);

        // Special case of new buffers
        if (__FILE_IsBuffered(stream) && !stream->_cnt) {
            TRACE("fwrite: buffer is new, resetting");
            // Either buffer is filled to the brim, ready to burst
            // or the buffer has just been ensured.
            if (__FILE_BytesBuffered(stream)) {
                int res = OsErrToErrNo(io_buffer_flush(stream));
                if (res) {
                    funlockfile(stream);
                    return res;
                }
            } else {
                stream->_cnt = stream->_bufsiz;
            }
        }

        // start by filling the entire write buffer
        // 3 Cases:
        // 1. (Buffered) Partially filled => fill & flush
        // 2. (Buffered) Empty, space for all => fill
        // 3. All other cases: write directly
        if (__FILE_IsBuffered(stream) && __FILE_BytesBuffered(stream)) {
            TRACE("fwrite: writing %i bytes into buffer", MIN(stream->_cnt, (int)wrcnt));
            // Flush as much as possible into the buffer in one go
            bytesWritten = __fill_buffer(stream, vptr, (int)wrcnt);
            if (bytesWritten > 0 && !stream->_cnt) {
                int ret = OsErrToErrNo(io_buffer_flush(stream));
                if (ret) {
                    stream->Flags |= _IOERR;
                    break;
                }
            }
        } else if (__FILE_IsBuffered(stream) && wrcnt < stream->_cnt) {
            TRACE("fwrite: filling %u bytes into buffer", wrcnt);
            bytesWritten = __fill_buffer(stream, vptr, (int)wrcnt);
        } else {
            TRACE("fwrite: writing %u bytes directly", wrcnt);
            bytesWritten = write(stream->IOD, vptr, wrcnt);
        }
        ERROR("fwrite: wrote %i bytes", bytesWritten);

        if (bytesWritten == -1) {
            stream->Flags |= _IOERR;
            break;
        } else if (bytesWritten > 0) {
            written += bytesWritten;
            wrcnt -= bytesWritten;
            vptr = (const char*)vptr + bytesWritten;
        }
	}
	funlockfile(stream);
	return written / size;
}
