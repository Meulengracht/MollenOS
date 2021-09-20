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
 * Standard C Library
 * - Returns a character back into the stream
 */

#include <stdio.h>
#include <internal/_io.h>

int ungetc(
    _In_ int character, 
    _In_ FILE *file)
{
    if (file == NULL)
        return EOF;

    if (character == EOF || !(file->_flag & _IOREAD || (file->_flag & _IORW && !(file->_flag & _IOWRT))))
        return EOF;

    _lock_stream(file);
    io_buffer_ensure(file);

    if (!(file->_flag & (_IONBF | _IOMYBUF | _USERBUF)) || (!file->_cnt && file->_ptr == file->_base))
        file->_ptr++;

    if (file->_ptr > file->_base) {
        file->_ptr--;
        
        if (file->_flag & _IOSTRG) {
            if (*file->_ptr != character) {
                file->_ptr++;
                _unlock_stream(file);
                return EOF;
            }
        }
        else {
            *file->_ptr = character;
        }

        file->_cnt++;
        clearerr(file);
        file->_flag |= _IOREAD;
        _unlock_stream(file);
        return character;
    }

    _unlock_stream(file);
    return EOF;
}
