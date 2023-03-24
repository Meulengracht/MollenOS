/**
 * Copyright 2022, Philip Meulengracht
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

#include "assert.h"
#include "errno.h"
#include "internal/_file.h"
#include "internal/_io.h"
#include "io.h"
#include "stdio.h"

long tell(
	_In_ int fd)
{
	return lseek(fd, 0, SEEK_CUR);
}

long long telli64(
	_In_ int fd)
{
	return lseeki64(fd, 0, SEEK_CUR);
}

long long ftelli64(
	_In_ FILE *stream)
{
    stdio_handle_t* handle;
	long long       position;

	if (!stream) {
		_set_errno(EINVAL);
		return -1LL;
	}
	
	flockfile(stream);
	handle = stdio_handle_get(stream->IOD);
    if (handle == NULL) {
		funlockfile(stream);
        _set_errno(EBADFD);
        return -1;
    }

	position = telli64(stream->IOD);
	if (position == -1) {
		funlockfile(stream);
		return -1;
	}

	// Buffered input? Then we have to modify the pointer
	if (stream->Flags & (_IOMYBUF | _IOUSRBUF)) {
		if (stream->StreamMode == __STREAMMODE_WRITE) {
			// Add the calculated difference in position
			position += stream->_ptr - stream->_base;

			// Extra special case in case of text stream
			if (handle->XTFlags & __IO_TEXTMODE) {
				char *p;

				for (p = stream->_base; p < stream->_ptr; p++) {
					if (*p == '\n') {
						position++;
					}
				}
			}
		} else if (!stream->_cnt) {
			// Empty buffer
		} else if (lseeki64(stream->IOD, 0, SEEK_END) == position) {
			int i;

			// Adjust for buffer count
			position -= stream->_cnt;

			// Special case for text streams again
			if (handle->XTFlags & __IO_TEXTMODE) {
				for (i = 0; i < stream->_cnt; i++) {
					if (stream->_ptr[i] == '\n') {
						position--;
					}
				}
			}
		} else {
			char *p;

			// Restore stream cursor in case we seeked to end
			if (lseeki64(stream->IOD, position, SEEK_SET) != position) {
				funlockfile(stream);
				return -1;
			}

			// Again adjust for the buffer
			position -= stream->_bufsiz;
			position += stream->_ptr - stream->_base;

			// And lastly, special text case
			if (handle->XTFlags & __IO_TEXTMODE) {
				if (handle->XTFlags & __IO_READNL) {
					position--;
				}

				for (p = stream->_base; p < stream->_ptr; p++) {
					if (*p == '\n') {
						position++;
					}
				}
			}
		}
	}
	funlockfile(stream);
	return position;
}

off_t ftello(
	_In_ FILE *stream)
{
	return (off_t)ftelli64(stream);
}

long ftell(
	_In_ FILE* stream)
{
	off_t offset = ftello(stream);
	if ((long)offset != offset) {
		_set_errno(EOVERFLOW);
		return -1L;
	}
	return offset;
}
