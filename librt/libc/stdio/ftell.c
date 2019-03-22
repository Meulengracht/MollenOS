/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS - C Standard Library
 * - Returns the current value of the position indicator of the stream.
 */

#include <os/services/file.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include "local.h"
#include <io.h>

/* tell 
 * Returns the current value of the position indicator of the stream.
 * ANSII Version of ftell */
long tell(
	_In_ int fd)
{
	return lseek(fd, 0, SEEK_CUR);
}

/* telli64 
 * Returns the current value of the position indicator of the stream.
 * ANSII Version of ftelli64 */
long long telli64(
	_In_ int fd)
{
	return lseeki64(fd, 0, SEEK_CUR);
}

/* ftelli64
 * Returns the current value of the position indicator of the stream. */
long long ftelli64(
	_In_ FILE *stream)
{
    StdioObject_t*  Object;
	long long       Position;

    assert(stream != NULL);

	_lock_file(stream);
    
	Object = get_ioinfo(stream->_fd);
    if (Object == NULL) {
		_unlock_file(stream);
        _set_errno(EBADFD);
        return -1;
    }

	Position = telli64(stream->_fd);
	if (Position == -1) {
		_unlock_file(stream);
		return -1;
	}

	// Buffered input? Then we have to modify the pointer
	if (stream->_flag & (_IOMYBUF | _USERBUF)) {
		if (stream->_flag & _IOWRT) {
			// Add the calculated difference in position
			Position += stream->_ptr - stream->_base;

			// Extra special case in case of text stream
			if (Object->wxflag & WX_TEXT) {
				char *p;

				for (p = stream->_base; p < stream->_ptr; p++) {
					if (*p == '\n') {
						Position++;
					}
				}
			}
		}
		else if (!stream->_cnt) {
			// Empty buffer
		}
		else if (lseeki64(stream->_fd, 0, SEEK_END) == Position) {
			int i;

			// Adjust for buffer count
			Position -= stream->_cnt;

			// Special case for text streams again
			if (Object->wxflag & WX_TEXT) {
				for (i = 0; i < stream->_cnt; i++) {
					if (stream->_ptr[i] == '\n') {
						Position--;
					}
				}
			}
		}
		else {
			char *p;

			// Restore stream cursor in case we seeked to end
			if (lseeki64(stream->_fd, Position, SEEK_SET) != Position) {
				_unlock_file(stream);
				return -1;
			}

			// Again adjust for the buffer
			Position -= stream->_bufsiz;
			Position += stream->_ptr - stream->_base;

			// And lastly, special text case
			if (Object->wxflag & WX_TEXT) {
				if (Object->wxflag & WX_READNL) {
					Position--;
				}

				for (p = stream->_base; p < stream->_ptr; p++) {
					if (*p == '\n') {
						Position++;
					}
				}
			}
		}
	}
	_unlock_file(stream);
	return Position;
}

/* ftello
 * Returns the current value of the position indicator of the stream. */
off_t ftello(
	_In_ FILE *stream)
{
	return (off_t)ftelli64(stream);
}

/* ftell
 * Returns the current value of the position indicator of the stream. */
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
