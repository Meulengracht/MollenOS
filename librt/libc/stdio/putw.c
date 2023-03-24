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
    _In_ int   val,
    _In_ FILE* file)
{
    int res = 0;

    flockfile(file);

    // Ensure that this mode is supported
    if (!__FILE_StreamModeSupported(file, __STREAMMODE_WRITE)) {
        funlockfile(file);
        errno = EACCES;
        return EOF;
    }

    // If we were previously reading, then we flush
    if (file->StreamMode == __STREAMMODE_READ) {
        fflush(file);
    }
    __FILE_SetStreamMode(file, __STREAMMODE_WRITE);

    // Ensure a buffer is present if supported
    io_buffer_ensure(file);

    if (__FILE_IsBuffered(file) && file->_cnt > 0) {
        *(int*)file->_ptr = val;
        file->_cnt -= sizeof(int);
        file->_ptr += sizeof(int);
    } else {
        res = _flswbuf(val, file);
    }
    return res ? res : val;
}
