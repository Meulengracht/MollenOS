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

#include <ddk/utils.h>
#include <internal/_io.h>
#include <internal/_utils.h>
#include <os/services/file.h>
#include <stdio.h>

FILE* tmpfile(void)
{
    oserr_t         osStatus;
    stdio_handle_t* object;
    uuid_t          handle;
    char            path[64];

    // generate a new path we can use as a temporary file
    sprintf(&path[0], "/tmp/%s",  tmpnam(NULL));
    osStatus = OSOpenPath(
            path,
            __FILE_CREATE | __FILE_TEMPORARY | __FILE_FAILONEXIST | __FILE_BINARY,
            __FILE_READ_ACCESS | __FILE_WRITE_ACCESS,
            &handle
    );
    if (OsErrToErrNo(osStatus)) {
        ERROR("tmpfile(path=%s) failed with code: %u", &path[0], osStatus);
        return NULL;
    }

    if (stdio_handle_create(-1, WX_DONTINHERIT | WX_TEMP, &object)) {
        osStatus = OSCloseFile(handle);
        if (osStatus != OsOK) {
            WARNING("tmpfile(path=%s) failed to cleanup handle");
        }
        return NULL;
    }
    
    stdio_handle_set_handle(object, handle);
    stdio_handle_set_ops_type(object, STDIO_HANDLE_FILE);

    return fdopen(object->fd, "wb+");
}
