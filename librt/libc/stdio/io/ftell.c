/* MollenOS
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
 * - Returns the current value of the position indicator of the stream.
 */

#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <internal/_io.h>
#include <io.h>

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
	
	_lock_stream(stream);
	handle = stdio_handle_get(stream->_fd);
    if (handle == NULL) {
		_unlock_stream(stream);
        _set_errno(EBADFD);
        return -1;
    }

	position = telli64(stream->_fd);
	if (position == -1) {
		_unlock_stream(stream);
		return -1;
	}

	// Buffered input? Then we have to modify the pointer
	if (stream->_flag & (_IOMYBUF | _USERBUF)) {
		if (stream->_flag & _IOWRT) {
			// Add the calculated difference in position
			position += stream->_ptr - stream->_base;

			// Extra special case in case of text stream
			if (handle->wxflag & WX_TEXT) {
				char *p;

				for (p = stream->_base; p < stream->_ptr; p++) {
					if (*p == '\n') {
						position++;
					}
				}
			}
		}
		else if (!stream->_cnt) {
			// Empty buffer
		}
		else if (lseeki64(stream->_fd, 0, SEEK_END) == position) {
			int i;

			// Adjust for buffer count
			position -= stream->_cnt;

			// Special case for text streams again
			if (handle->wxflag & WX_TEXT) {
				for (i = 0; i < stream->_cnt; i++) {
					if (stream->_ptr[i] == '\n') {
						position--;
					}
				}
			}
		}
		else {
			char *p;

			// Restore stream cursor in case we seeked to end
			if (lseeki64(stream->_fd, position, SEEK_SET) != position) {
				_unlock_stream(stream);
				return -1;
			}

			// Again adjust for the buffer
			position -= stream->_bufsiz;
			position += stream->_ptr - stream->_base;

			// And lastly, special text case
			if (handle->wxflag & WX_TEXT) {
				if (handle->wxflag & WX_READNL) {
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
	_unlock_stream(stream);
	return position;
}

off_t ftello(
	_In_ FILE *stream)
{
	return (off_t)ftelli64(stream);
}

long ftell(
	_In_ FILE *stream)
{
	off_t rOffset = ftello(stream);
	if ((long)rOffset != rOffset) {
		_set_errno(EOVERFLOW);
		return -1L;
	}
	return rOffset;
}
