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

#include <io.h>
#include <internal/_io.h>
#include <internal/_file.h>
#include <stdio.h>

static int __fill_buffer(
	_In_ FILE* file)
{
	unsigned char c;
    
	// We can't fill the io buffer if this is a strange resource
	if (file->_flag & _IOSTRG) {
		return EOF;
	}

	io_buffer_ensure(file);
	if (stream_ensure_mode(_IOREAD, file)) {
	    return EOF;
	}

	// Is this even a buffered input? If not only read one byte at the time
	if (IO_IS_NOT_BUFFERED(file)) {
		int r;
		
		// Read a single byte
		if ((r = read(file->_fd, &c, 1)) != 1) {
			file->_flag |= (r == 0) ? _IOEOF : _IOERR;
			return EOF;
		}
		return c;
	}

    // Now actually fill the buffer
    file->_cnt = read(file->_fd, file->_base, file->_bufsiz);

    // If it failed, we are either at end of file or encounted
    // a real error
    if (file->_cnt <= 0) {
        file->_flag |= (file->_cnt == 0) ? _IOEOF : _IOERR;
        file->_cnt = 0;
        return EOF;
    }

    // Reduce count and return top of buffer
    file->_cnt--;
    file->_ptr = file->_base + 1;
    c = *(unsigned char *)file->_base;
    return c;
}

int fgetc(
	_In_ FILE *file)
{
	unsigned char* i;
	unsigned int   j;

	// Check buffer before filling/raw-reading
    flockfile(file);
	if (file->_cnt > 0) {
		file->_cnt--;
		i = (unsigned char *)file->_ptr++;
		j = *i;
	} else {
		j = __fill_buffer(file);
	}
    funlockfile(file);
	return (int)j;
}
