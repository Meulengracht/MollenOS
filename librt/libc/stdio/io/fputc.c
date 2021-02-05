/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * C Standard Library
 * - Writes a character to the stream and advances the position indicator.
 */

#include <stdio.h>
#include <internal/_io.h>

int fputc(
    _In_ int   character,
    _In_ FILE* file)
{
    int res = 0;

    _lock_file(file);
    if (file->_cnt > 0) {
        file->_cnt--;
        *file->_ptr++ = (char)(character & 0xFF);

        if ((file->_flag & _IOLBF) && character == '\n') {
            res = io_buffer_flush(file);
        }
    }
    else {
        res = _flsbuf(character, file);
    }

    _unlock_file(file);
    return res ? res : character;
}
