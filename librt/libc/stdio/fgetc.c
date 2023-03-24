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
#include <internal/_io.h>
#include <internal/_file.h>
#include <stdio.h>

static int
__fill_buffer(
	_In_ FILE* file)
{
	// We can't fill the io buffer if this is a strange resource
	if (__FILE_IsStrange(file)) {
		return EOF;
	}

    // Try to ensure a buffer is present if possible.
	io_buffer_ensure(file);

    // Now actually fill the buffer
    file->_cnt = read(file->IOD, file->_base, file->_bufsiz);

    // If it failed, we are either at end of file or encounted
    // a real error
    if (file->_cnt < 1) {
        file->Flags |= (file->_cnt == 0) ? _IOEOF : _IOERR;
        file->_cnt = 0;
        return EOF;
    }

    // reduce number of available bytes with 1 and return
    // the first character
    file->_cnt--;
    file->_ptr = file->_base + 1;
    return *(unsigned char *)file->_base;
}

int fgetc(
	_In_ FILE *file)
{
	unsigned char* i;
	unsigned int   j;

	// Check buffer before filling/raw-reading
    flockfile(file);

    // Ensure that this mode is supported
    if (!__FILE_StreamModeSupported(file, __STREAMMODE_READ)) {
        funlockfile(file);
        errno = EACCES;
        return EOF;
    }

    // If we were previously writing, then we flush
    if (file->StreamMode == __STREAMMODE_WRITE) {
        fflush(file);
    }
    __FILE_SetStreamMode(file, __STREAMMODE_READ);

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
