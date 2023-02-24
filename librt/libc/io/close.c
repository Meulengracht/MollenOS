/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <errno.h>
#include <io.h>
#include <internal/_io.h>
#include <internal/_file.h>

int close(int fd)
{
    stdio_handle_t* handle;
    int             options = 0;

    handle = stdio_handle_get(fd);
    if (!handle) {
        _set_errno(EBADFD);
        return -1;
    }

    // The cases where we close is when the handle is
    // not inheritted or the handle is not persistant
    if (!(handle->wxflag & (WX_INHERITTED | WX_PERSISTANT))) {
        options |= STDIO_CLOSE_FULL;
    }

    handle->ops.close(handle, options);
    stdio_handle_destroy(handle);
    return 0;
}
