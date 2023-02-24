/**
 * MollenOS
 *
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
 *
 *
 * Standard C Library
 * - Reads an array of count elements, 
 *   each one with a size of size bytes, from the stream and stores 
 *   them in the block of memory specified by ptr.
 */

#define __need_minmax
//#define __TRACE
#include "ddk/utils.h"
#include "errno.h"
#include "internal/_file.h"
#include "internal/_io.h"
#include "io.h"
#include "stdio.h"
#include <limits.h>

size_t
fread(void *vptr, size_t size, size_t count, FILE *stream)
{
	size_t rcnt = size * count;
	size_t cread = 0;
	size_t pread = 0;
	TRACE("fread(vptr=0x%" PRIxIN ", size=%" PRIuIN ", count=%" PRIuIN ", stream=0x%" PRIxIN ", stream->_flag=%i)",
       vptr, size, count, stream, stream ? stream->_flag : 0);

	if (!rcnt) {
	    _set_errno(EINVAL);
		return 0;
	}

	flockfile(stream);
	if (stream_ensure_mode(_IOREAD, stream)) {
	    goto exit;
	}
    io_buffer_ensure(stream);

	// so check the buffer for any data
	if (IO_HAS_BUFFER_DATA(stream)) {
		int pcnt = (rcnt > stream->_cnt) ? stream->_cnt : rcnt;
		memcpy(vptr, stream->_ptr, pcnt);
		
		stream->_cnt -= pcnt;
		stream->_ptr += pcnt;
		
		cread += pcnt;
		rcnt -= pcnt;
		vptr = (char *)vptr + pcnt;
	}

	// Keep reading untill all requested bytes are read, or EOF
	while (rcnt > 0) {
		int i;

		// if buffer is empty and the data fits into the buffer, then we fill that instead
		if (IO_IS_BUFFERED(stream) && !stream->_cnt && rcnt < BUFSIZ) {
			stream->_cnt = read(stream->_fd, stream->_base, stream->_bufsiz);
			stream->_ptr = stream->_base;
			i = (stream->_cnt < rcnt) ? stream->_cnt : rcnt;

			/* If the buffer fill reaches eof but fread wouldn't, clear eof. */
			if (i > 0 && i < stream->_cnt) {
				stdio_handle_get(stream->_fd)->wxflag &= ~WX_ATEOF;
				stream->_flag &= ~_IOEOF;
			}
			
			if (i > 0) {
				memcpy(vptr, stream->_ptr, i);
				stream->_cnt -= i;
				stream->_ptr += i;
			}
		}
		else if (rcnt > INT_MAX) {
			i = read(stream->_fd, vptr, INT_MAX);
		}
		else if (rcnt < BUFSIZ) {
			i = read(stream->_fd, vptr, rcnt);
		}
		else {
			i = read(stream->_fd, vptr, rcnt - BUFSIZ / 2);
		}

		// update iterators
		pread += i;
		rcnt -= i;
		vptr = (char *)vptr + i;

		// Check for EOF condition
		// also for error conditions
		if (stdio_handle_get(stream->_fd)->wxflag & WX_ATEOF) {
			stream->_flag |= _IOEOF;
		}
		else if (i == -1) {
			stream->_flag |= _IOERR;
			pread = 0;
			rcnt = 0;
		}

		// Break if bytes read is 0 or below
		if (i < 1) {
			break;
		}
	}

	// Increase the number of bytes read
	cread += pread;

exit:
    funlockfile(stream);
    TRACE("fread returns=%" PRIuIN ", errno=%i", (cread / size), errno);
	return (cread / size);
}
