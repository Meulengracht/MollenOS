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
#include <internal/_file.h>
#include <internal/_io.h>
#include <io.h>
#include <os/mollenos.h>
#include <stdio.h>

int fputc(
    _In_ int   character,
    _In_ FILE* file)
{
    int written;
    int res = character;

    flockfile(file);

    // Ensure that this mode is supported
    if (!__FILE_CanWrite(file)) {
        funlockfile(file);
        errno = EACCES;
        return EOF;
    }

    // We ensure a buffer is present on all buffered calls.
    io_buffer_ensure(file);

    // Is the buffer-space available?
    if (__FILE_IsBuffered(file)) {
        int bytesAvailable = file->BufferSize - __FILE_BufferPosition(file);
        if (bytesAvailable < sizeof(char)) {
            // Should we be out of buffer space for a strange file-descriptor, then
            // we return EBUFFER instead of EACCESS to indicate that we ran out of
            // buffer.
            if (__FILE_IsStrange(file)) {
                funlockfile(file);
                errno = ENOBUFS;
                return -1;
            }

            if (fflush(file)) {
                funlockfile(file);
                return -1;
            }
        }

        // At this point we *must* have buffer room, if not we error
        bytesAvailable = file->BufferSize - __FILE_BufferPosition(file);
        if (bytesAvailable < sizeof(char)) {
            funlockfile(file);
            errno = ENOBUFS;
            return -1;
        }
        *(file->Current) = (char)(character & 0xFF);
        file->Current += sizeof(char);
        file->Flags |= _IOMOD;
        __FILE_UpdateBytesValid(file);
        written = sizeof(char);
    } else {
        if (__FILE_IsStrange(file)) {
            funlockfile(file);
            errno = EACCES;
            return -1;
        }
        __FILE_ResetBuffer(file);
        written = write(file->IOD, &character, sizeof(char));
    }

    if (written != sizeof(char)) {
        file->Flags |= _IOERR;
        res = EOF;
    }
    funlockfile(file);
    return res;
}
