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

int fgetc(
	_In_ FILE *file)
{
	unsigned char* i;
	unsigned int   j;

	// Check buffer before filling/raw-reading
    flockfile(file);

    // Ensure that this mode is supported
    if (!__FILE_CanRead(file)) {
        funlockfile(file);
        errno = EACCES;
        return EOF;
    }

    // Ensure a buffer is present if possible. We need it before reading
    io_buffer_ensure(file);

    // Preread from the internal buffer first, otherwise do a direct read
    // if the underlying IOD allows us.
    if (__FILE_BufferBytesForReading(file) > 0) {
		i = (unsigned char *)file->Current++;
		j = *i;
	} else {
        int ret;

        if (__FILE_IsStrange(file)) {
            funlockfile(file);
            return EOF;
        }

        if (__FILE_IsBuffered(file)) {
            // Refill the buffer from the underlying IOD, and reset stream
            // pointer
            ret = read(file->IOD, file->Base, file->BufferSize);
            if (ret > 0) {
                file->BytesValid = ret;
                file->Current = file->Base + sizeof(char);

                i = (unsigned char *)file->Base;
                j = *i;
            }
        } else {
            // Do a direct read
            ret = read(file->IOD, &j, sizeof(char));
            if (ret > 0) {
                j &= 0x000000FF;
            }
        }

        if (ret <= 0) {
            if (ret < 0) {
                file->Flags |= _IOERR;
            } else {
                file->Flags |= _IOEOF;
            }
            j = (unsigned int)EOF;
        }
	}
    funlockfile(file);
	return (int)j;
}
