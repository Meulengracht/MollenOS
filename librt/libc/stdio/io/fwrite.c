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

//#define __TRACE

#include <ddk/utils.h>
#include <errno.h>
#include <io.h>
#include <internal/_io.h>
#include <os/mollenos.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int write(int fd, const void* buffer, unsigned int length)
{
	stdio_handle_t* handle       = stdio_handle_get(fd);
	size_t          bytesWritten = 0;
	int             res;
	oserr_t      status;
	TRACE("write(fd=%i, buffer=0x%" PRIxIN ", length=%u)", fd, buffer, length);

	// Don't write uneven bytes in case of UTF8/16
	if ((handle->wxflag & WX_UTF) == WX_UTF && (length & 1)) {
		_set_errno(EINVAL);
		res = -1;
		goto exit;
	}

	// If appending, go to EOF
	if (handle->wxflag & WX_APPEND) {
		lseek(fd, 0, SEEK_END);
	}

	// If we aren't in text mode, raw write the data without any text-processing
    status = handle->ops.write(handle, (char*)buffer, length, &bytesWritten);
	if (status != OsOK) {
	    res = OsErrToErrNo(status);
	} else {
	    res = (int)bytesWritten;
	}

exit:
    TRACE("write return=%i (errno %i)", res, errno);
	return res;
}

size_t fwrite(const void* vptr, size_t size, size_t count, FILE* stream)
{
	size_t wrcnt = size * count;
	int written = 0;

	if (!vptr || !size || !count || !stream) {
		_set_errno(EINVAL);
		return 0;
	}
	
	// Write the bytes in a loop in case we can't
	// flush it all at once
	_lock_stream(stream);
	while (wrcnt) {

		// Sanitize output buffer count
		if (stream->_cnt < 0) {
			stream->_flag |= _IOERR;
			break;
		}
		else if (stream->_cnt) {
			// Flush as much as possible into the buffer in one go
			int pcnt = (stream->_cnt > wrcnt) ? wrcnt : stream->_cnt;
			memcpy(stream->_ptr, vptr, pcnt);

			// Adjust
			stream->_cnt -= pcnt;
			stream->_ptr += pcnt;

			// Update pointers
			written += pcnt;
			wrcnt -= pcnt;
			vptr = (const char *)vptr + pcnt;
		}
		else if ((stream->_flag & _IONBF) 
			|| ((stream->_flag & (_IOMYBUF | _USERBUF)) 
				&& wrcnt >= stream->_bufsiz) 
			|| (!(stream->_flag & (_IOMYBUF | _USERBUF)) 
				&& wrcnt >= INTERNAL_BUFSIZ)) {
			// Variables
			size_t pcnt;
			int bufsiz;

			// Handle the kind of buffer type thats specified
			if (stream->_flag & _IONBF) {
				bufsiz = 1;
			}
			else if (!(stream->_flag & (_IOMYBUF | _USERBUF))) {
				bufsiz = INTERNAL_BUFSIZ;
			}
			else {
				bufsiz = stream->_bufsiz;
			}
			pcnt = (wrcnt / bufsiz) * bufsiz;

			// Flush stream buffer
			if (io_buffer_flush(stream) != OsOK) {
				break;
			}

			// Write buffer to stream
			if (write(stream->_fd, vptr, pcnt) <= 0) {
				stream->_flag |= _IOERR;
				break;
			}

			// Update pointers
			written += pcnt;
			wrcnt -= pcnt;
			vptr = (const char *)vptr + pcnt;
		}
		else {
			// Fill buffer
			if (_flsbuf(*(const char *)vptr, stream) == EOF) {
				break;
			}

			// Update pointers
			written++;
			wrcnt--;
			vptr = (const char *)vptr + 1;
		}
	}

	// Unlock stream and return member-count written
	_unlock_stream(stream);
	return written / size;
}
