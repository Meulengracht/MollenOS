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

int putw(
    _In_ int   ch,
    _In_ FILE* file)
{
    int written;
    int res = ch;

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
        if (bytesAvailable < sizeof(wchar_t)) {
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
        if (bytesAvailable < sizeof(wchar_t)) {
            funlockfile(file);
            errno = ENOBUFS;
            return -1;
        }
        *((wchar_t*)file->Current) = (wchar_t)(ch & 0xFFFF);
        file->Current += sizeof(wchar_t);
        file->Flags |= _IOMOD;
        __FILE_UpdateBytesValid(file);
        written = sizeof(wchar_t);
    } else {
        if (__FILE_IsStrange(file)) {
            funlockfile(file);
            errno = EACCES;
            return -1;
        }
        __FILE_ResetBuffer(file);
        written = write(file->IOD, &ch, sizeof(wchar_t));
    }

    if (written != sizeof(wchar_t)) {
        file->Flags |= _IOERR;
        res = EOF;
    }
    funlockfile(file);
    return res;
}
