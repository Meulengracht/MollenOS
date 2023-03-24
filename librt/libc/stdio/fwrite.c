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
    memcpy(stream->_ptr, buffer, count);
    stream->_cnt -= count;
    stream->_ptr += count;
    return count;
}

size_t fwrite(const void* vptr, size_t size, size_t count, FILE* stream)
{
	size_t wrcnt = size * count;
	int    written = 0;

	if (!vptr || !size || !count || !stream) {
		_set_errno(EINVAL);
		return 0;
	}

	flockfile(stream);

    // Ensure that this mode is supported
    if (!__FILE_StreamModeSupported(stream, __STREAMMODE_WRITE)) {
        funlockfile(stream);
        errno = EACCES;
        return EOF;
    }
    __FILE_SetStreamMode(stream, __STREAMMODE_WRITE);

    io_buffer_ensure(stream);

    while (wrcnt) {
        int pcnt;

        // Special case of new buffers
        if (__FILE_IsBuffered(stream) && !stream->_cnt) {
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
        if (__FILE_IsBuffered(stream) && stream->_cnt) {
            // Flush as much as possible into the buffer in one go
            pcnt = __fill_buffer(stream, vptr, MIN(stream->_cnt, (int)wrcnt));
        } else if (__FILE_IsBuffered(stream) && wrcnt < stream->_bufsiz) {
            pcnt = __fill_buffer(stream, vptr, (int)wrcnt);
        } else {
            pcnt = write(stream->IOD, vptr, wrcnt);
        }

        written += pcnt;
        wrcnt -= pcnt;
        vptr = (const char*)vptr + pcnt;
	}
	funlockfile(stream);
	return written / size;
}
