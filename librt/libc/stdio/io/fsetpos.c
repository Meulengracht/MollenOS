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

#include <errno.h>
#include <io.h>
#include <internal/_file.h>
#include <internal/_io.h>
#include <stdio.h>

int fsetpos(
	_In_ FILE*         stream, 
	_In_ const fpos_t* pos)
{
	int ret;

	if (!stream) {
		_set_errno(EINVAL);
		return -1;
	}

    flockfile(stream);

	// If the input stream is buffered we flush it
	if (stream->_flag & _IOWRT) {
        io_buffer_flush(stream);
	}

	// Discard buffered input
	stream->_cnt = 0;
	stream->_ptr = stream->_base;

	// Reset direction of i/o
	if (stream->_flag & _IORW) {
		stream->_flag &= ~(_IOREAD | _IOWRT);
	}

	// Now actually set the position
	ret = (lseeki64(stream->_fd, *pos, SEEK_SET) == -1) ? -1 : 0;
	funlockfile(stream);
	return ret;
}
