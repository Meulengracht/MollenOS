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
 * MollenOS C Library - Write to file-handles
 */

/* Includes
 * - System */
#include <os/file.h>
#include <os/syscall.h>

/* Includes 
 * - Library */
#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "local.h"

/* _write
 * This is the ANSI C version of fwrite */
int _write(
	_In_ int fd, 
	_In_ void *buffer, 
	_In_ unsigned int length)
{
	// Variables
	StdioObject_t *Info = get_ioinfo(fd);
	size_t BytesWritten = 0;

	// Don't write uneven bytes in case of UTF8/16
	if (((Info->exflag & EF_UTF8) 
		|| (Info->exflag & EF_UTF16)) && (length & 1)) {
		_set_errno(EINVAL);
		return -1;
	}

	// If appending, go to EOF
	if (Info->wxflag & WX_APPEND) {
		_lseek(fd, 0, SEEK_END);
	}

	// If we aren't in text mode, raw write the data
	// without any text-processing
	if (StdioWriteInternal(fd, (char*)buffer, length, &BytesWritten) == OsSuccess) {
		return (int)BytesWritten;
	}

	// Set error code
	_set_errno(ENOSPC);
	return -1;
}

/* The fwrite
* writes to a file handle */
size_t fwrite(
	_In_ __CONST void *vptr,
	_In_ size_t size,
	_In_ size_t count,
	_In_ FILE *stream)
{
	// Variables
	size_t wrcnt = size * count;
	int written = 0;

	// Sanitize param
	if (size == 0) {
		return 0;
	}

	// lock stream access
	_lock_file(stream);

	// Write the bytes in a loop in case we can't
	// flush it all at once
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
			if (os_flush_buffer(stream) != OsSuccess) {
				break;
			}

			// Write buffer to stream
			if (_write(stream->_fd, (void*)vptr, pcnt) <= 0) {
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
	_unlock_file(stream);
	return written / size;
}
