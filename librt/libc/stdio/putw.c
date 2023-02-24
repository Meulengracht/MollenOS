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

#include "io.h"
#include "internal/_io.h"
#include "internal/_file.h"
#include "stdio.h"

int putw(
    _In_ int   val,
    _In_ FILE* file)
{
    int len;

    flockfile(file);
    len = write(file->_fd, &val, sizeof(val));
    if (len == sizeof(val)) {
        funlockfile(file);
        return val;
    }

    file->_flag |= _IOERR;
    funlockfile(file);
    return EOF;
}
