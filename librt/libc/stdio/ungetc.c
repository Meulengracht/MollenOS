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
#include <internal/_io.h>
#include <internal/_file.h>
#include <stdio.h>

int ungetc(
    _In_ int   character,
    _In_ FILE* file)
{
    int bytesAvailable;

    if (file == NULL || character == EOF) {
        errno = EINVAL;
        return EOF;
    }

    flockfile(file);
    if (!__FILE_CanRead(file)) {
        errno = EACCES;
        goto eof;
    }

    // When doing buffered operations we must always make sure there
    // is a valid buffer. Even for unbuffered streams, in the case of
    // ungetc
    io_buffer_ensure(file);

    // Is there space in the buffer?
    // TODO: is ungetc actually supposed to flush?
    bytesAvailable = file->BufferSize - __FILE_BufferPosition(file);
    if (!bytesAvailable) {
        errno = ENOBUFS;
        goto eof;
    }

    // We have a special case of being at the first byte of the
    // buffer, in this case we cannot just go back one
    if (__FILE_BufferPosition(file) == 0) {
        file->Current++;
        __FILE_UpdateBytesValid(file);
    } else {
        file->Current--;
    }
    *(file->Current) = (char)(character & 0xFF);
    clearerr(file);
    return character;

eof:
    funlockfile(file);
    return EOF;
}
