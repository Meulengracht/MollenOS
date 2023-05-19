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

//#define __TRACE
#define __need_minmax
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_file.h>
#include <internal/_io.h>
#include <io.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

// Case ((_pos = (ptr - base)))
// Write only
// 1. Write 56 bytes [_cnt=0;  _pos=56]
// 2. Write 200 bytes [_cnt=0; _pos=256]
// Read only
// 1. Fill [_cnt=8196, _pos=0]
// 2. Read 56 bytes [_cnt=8140, _pos=56]
// 3. Read 140 bytes [_cnt=8000, _pos=196]
// Read-write-read
// 1. Fill [_cnt=8196, _pos=0]
// 2. Read 56 bytes [_cnt=8140, _pos=56]
// 3. Write 140 bytes [_cnt=8000, _pos=196, mod=true]
// 4. Read 400 bytes [_cnt=7600, _pos=596, mod=true]
// Write-read-write
// 1. Write 56 bytes [_cnt=0; _pos=56, mod=true]
// 2. Flush [_cnt=0, _pos=0]
// 3. Fill [_cnt=8196, _pos=0]
// 4. Read 56 bytes [_cnt=8140, _pos=56]

static int
__preread_buffer(
        _In_ FILE*  stream,
        _In_ void*  buffer,
        _In_ int    count)
{
    int bytesToCopy = MIN(count, __FILE_BufferBytesForReading(stream));
    if (bytesToCopy <= 0) {
        return 0;
    }

    memcpy(buffer, stream->Current, bytesToCopy);

    stream->Current += bytesToCopy;
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

    flockfile(stream);
    if (!__FILE_CanRead(stream)) {
        ERROR("fread: not supported");
        stream->Flags |= _IOERR;
        funlockfile(stream);
        errno = EACCES;
        return 0;
    }

    // Ensure a buffer is present if possible. We need it before reading.
    io_buffer_ensure(stream);

    // Always pre-read from the internal buffer. Even when streams are unbuffered
    // we have a small ungetc buffer which needs to be emptied first.
    bytesRead = __preread_buffer(stream, vptr, (int)rcnt);
    if (bytesRead) {
        TRACE("fread: read %i bytes from internal buffer", bytesRead);
        cread += bytesRead;
        rcnt -= bytesRead;
        vptr = (char*)vptr + bytesRead;
    }

    // After pre-reading the buffer we may need to flush it if it was
    // modified or empty. The number of bytes available for reading must
    // return 0 to indicate it's empty.
    if (__FILE_BufferBytesForReading(stream) == 0) {
        if (fflush(stream)) {
            funlockfile(stream);
            return -1;
        }
    }

	// Keep reading untill all requested bytes are read, or EOF. We can make the assumption
    // when doing this while loop, then the buffer is empty.
	while (rcnt > 0) {
        int chunkSize = MIN(rcnt, INT_MAX);
        TRACE("fread: %u bytes left", rcnt);

        // We cannot perform reading from the underlying IOD if this is
        // a strange resource
        if (__FILE_IsStrange(stream)) {
            ERROR("fread: cannot read from underlying iod with strange streams");
            stream->Flags |= _IOERR;
            break;
        }

		// If buffer is empty and the data fits into the buffer, then we fill that instead
		if (__FILE_IsBuffered(stream) && chunkSize < stream->BufferSize) {
            TRACE("fread: filling read buffer of size %i", stream->BufferSize);
			int ret = read(stream->IOD, stream->Base, stream->BufferSize);
            if (ret > 0) {
                stream->BytesValid = ret;
                stream->Current = stream->Base;
                bytesRead = MIN(stream->BytesValid, chunkSize);
                if (bytesRead > 0) {
                    memcpy(vptr, stream->Current, bytesRead);
                    stream->Current += bytesRead;
                }
            }
        } else {
            TRACE("fread: filling buffer directly of size %i", chunkSize);
            bytesRead = read(stream->IOD, vptr, chunkSize);
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
            vptr = (char*)vptr + bytesRead;
        }
	}
    funlockfile(stream);
	return ((cread + pread) / size);
}
