/**
 * MollenOS
 *
 * Copyright 2021, Philip Meulengracht
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
 * - File link implementation
 */

#include <stdio.h>
#include <internal/_io.h>

FILE* tmpfile(void)
{
    stdio_handle_t* handle;
    char            path[64];
    FILE*           file;

    sprintf(&path[0], "$share/%s",  tmpnam(NULL));
    file = fopen(&path[0], "wb+");
    if (!file) {
        return NULL;
    }

    // set delete on close
    handle = stdio_handle_get(fileno(file));
    if (handle) {
        handle->wxflag |= WX_TEMP;
    }

    return file;
}
