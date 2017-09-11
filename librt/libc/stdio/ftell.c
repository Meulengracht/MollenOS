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

/* Includes
 * - System */
#include <os/driver/file.h>
#include <os/syscall.h>

/* Includes 
 * - Library */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "local.h"

/* _tell 
 * Returns the current value of the position indicator of the stream.
 * ANSII Version of ftell */
long _tell(
	_In_ int fd)
{
	return _lseek(fd, 0, SEEK_CUR);
}

/* _telli64 
 * Returns the current value of the position indicator of the stream.
 * ANSII Version of ftelli64 */
long long _telli64(
	_In_ int fd)
{
	return _lseeki64(fd, 0, SEEK_CUR);
}

/* ftelli64
 * Returns the current value of the position indicator of the stream. */
long long ftelli64(
	_In_ FILE *stream)
{
	// Variables
	long long pos;
	
	// Lock access
	_lock_file(stream);

	// Get initial position
	pos = _telli64(stream->_fd);
	if (pos == -1) {
		_unlock_file(stream);
		return -1;
	}

	// Buffered input? Then we have to modify the pointer
	if (stream->_flag & (_IOMYBUF | _USERBUF)) {
		if (stream->_flag & _IOWRT) {
			// Add the calculated difference in position
			pos += stream->_ptr - stream->_base;

			// Extra special case in case of text stream
			if (get_ioinfo(stream->_fd)->wxflag & WX_TEXT) {
				char *p;

				for (p = stream->_base; p < stream->_ptr; p++) {
					if (*p == '\n') {
						pos++;
					}
				}
			}
		}
		else if (!stream->_cnt) {
			// Empty buffer
		}
		else if (_lseeki64(stream->_fd, 0, SEEK_END) == pos) {
			int i;

			// Adjust for buffer count
			pos -= stream->_cnt;

			// Special case for text streams again
			if (get_ioinfo(stream->_fd)->wxflag & WX_TEXT) {
				for (i = 0; i < stream->_cnt; i++) {
					if (stream->_ptr[i] == '\n') {
						pos--;
					}
				}
			}
		}
		else {
			char *p;

			// Restore stream cursor in case we seeked to end
			if (_lseeki64(stream->_fd, pos, SEEK_SET) != pos) {
				_unlock_file(stream);
				return -1;
			}

			// Again adjust for the buffer
			pos -= stream->_bufsiz;
			pos += stream->_ptr - stream->_base;

			// And lastly, special text case
			if (get_ioinfo(stream->_fd)->wxflag & WX_TEXT) {
				if (get_ioinfo(stream->_fd)->wxflag & WX_READNL) {
					pos--;
				}

				for (p = stream->_base; p < stream->_ptr; p++) {
					if (*p == '\n') {
						pos++;
					}
				}
			}
		}
	}

	// Unlock and return the modified position
	_unlock_file(stream);
	return pos;
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
	// Variables
	off_t rOffset;
	rOffset = ftello(stream);

	// Overflow check
	if ((long)rOffset != rOffset)
	{
		_set_errno(EOVERFLOW);
		return -1L;
	}
	return rOffset;
}
