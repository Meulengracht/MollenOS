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
#include <os/mollenos.h>
#include <stdio.h>

int fputc(
    _In_ int   character,
    _In_ FILE* file)
{
    int res = 0;

    flockfile(file);

    // Ensure that this mode is supported
    if (!__FILE_CanWrite(file)) {
        funlockfile(file);
        errno = EACCES;
        return EOF;
    }

    // Ensure a buffer is present if possible. We need it before reading
    io_buffer_ensure(file);

    if (__FILE_IsBuffered(file) && file->_cnt > 0) {
        file->_cnt--;
        *file->_ptr++ = (char)(character & 0xFF);

        // Even if we have space left, we should handle line-buffering
        // at this step.
        if ((file->Flags & _IOLBF) && character == '\n') {
            res = OsErrToErrNo(io_buffer_flush(file));
        }
    } else {
        res = _flsbuf(character, file);
    }
    funlockfile(file);
    return res ? res : character;
}
