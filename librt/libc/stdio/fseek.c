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
 * - Sets the position indicator associated with the stream to a new position.
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
#include "local.h"

/* _lseeki64
 * Sets the position indicator associated with the stream to a new position. 
 * ANSII Version of fseeki64. */
long long _lseeki64(
	_In_ int fd, 
	_In_ long long offset, 
	_In_ int whence)
{
	// Variables
	long long Position = 0;

	// Sanitize parameters
	if (whence < 0 || whence > 2) {
		_set_errno(EINVAL);
		return -1;
	}

	// Use the internal wrapper for setting position
	if (StdioSeekInternal(fd, offset, whence, &Position) != OsSuccess) {
		return -1L;
	}
	else {
		get_ioinfo(fd)->wxflag &= ~(WX_ATEOF|WX_READEOF);
		return Position;
	}
}

/* _lseek
 * Sets the position indicator associated with the stream to a new position. 
 * ANSII Version of fseek. */
long _lseek(
	_In_ int fd,
	_In_ long offset,
	_In_ int whence)
{
	return (long)_lseeki64(fd, offset, whence);
}

/* fseeki64
 * Sets the position indicator associated with the stream to a new position. */
int fseeki64(
	_In_ FILE *file, 
	_In_ long long offset, 
	_In_ int whence)
{
	// Variables
	int ret;

	// Lock access to stream
	_lock_file(file);

	// Flush output if needed
	if (file->_flag & _IOWRT) {
		os_flush_buffer(file);
	}

	// Adjust for current position
	if (whence == SEEK_CUR && (file->_flag & _IOREAD)) {
		whence = SEEK_SET;
		offset += ftelli64(file);
	}

	// Discard buffered input
	file->_cnt = 0;
	file->_ptr = file->_base;

	// Reset direction of i/o
	if (file->_flag & _IORW) {
		file->_flag &= ~(_IOREAD | _IOWRT);
	}

	// Clear end of file flag
	file->_flag &= ~_IOEOF;
	ret = (_lseeki64(file->_fd, offset, whence) == -1) ? -1 : 0;

	// Unlock and return
	_unlock_file(file);
	return ret;
}

/* fseek
 * Sets the position indicator associated with the stream to a new position. */
int fseek(
	_In_ FILE *stream,
	_In_ long int offset,
	_In_ int origin)
{
	return fseeki64(stream, offset, origin);
}

/* fseeko
 * Sets the position indicator associated with the stream to a new position. */
int fseeko(
	_In_ FILE *stream,
	_In_ off_t offset,
	_In_ int origin)
{
	return fseeki64(stream, offset, origin);
}
