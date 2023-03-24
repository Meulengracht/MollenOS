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

#include "internal/_io.h"
#include "internal/_file.h"
#include "stdio.h"

int ungetc(
    _In_ int   character,
    _In_ FILE* file)
{
    if (file == NULL) {
        return EOF;
    }

    flockfile(file);

    // Don't put anything weird back
    if (character == EOF) {
        goto eof;
    }

    // The stream must be in read mode
    if (file->StreamMode != __STREAMMODE_READ) {
        goto eof;
    }

    io_buffer_ensure(file);

    // The stream must have space in it's buffer. Even unbuffered
    // streams have space for a look-ahead
    if (!file->_cnt && file->_ptr == file->_base) {
        // Special case of unfilled buffers. This is still valid
        // to ungetc for.
        file->_ptr++;
    }

    if (file->_ptr > file->_base) {
        file->_ptr--;
        
        if (__FILE_IsStrange(file)) {
            if (*file->_ptr != character) {
                file->_ptr++;
                goto eof;
            }
        } else {
            *file->_ptr = (char)character;
        }

        file->_cnt++;
        clearerr(file);
        funlockfile(file);
        return character;
    }

eof:
    funlockfile(file);
    return EOF;
}
