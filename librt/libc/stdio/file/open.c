/* MollenOS
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
 * - Standard IO file operation implementations.
 */

#include <ddk/services/file.h> // for ipc
#include <os/services/file.h>
#include <ddk/utils.h>

#include <io.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <internal/_io.h>

// Convert O_* flags to WX_* flags
static unsigned
oftowx(unsigned oflags)
{
    int         wxflags = 0;
    unsigned    unsupp; // until we support everything

    if (oflags & O_APPEND)               wxflags |= WX_APPEND;
    if (oflags & O_BINARY)               {/* Nothing to do */}
    else if (oflags & O_TEXT)            wxflags |= WX_TEXT;
    else if (oflags & O_WTEXT)           wxflags |= WX_TEXT|WX_WIDE;
    else if (oflags & O_U16TEXT)         wxflags |= WX_TEXT|WX_UTF|WX_WIDE;
    else if (oflags & O_U8TEXT)          wxflags |= WX_TEXT|WX_UTF;
    else                                 wxflags |= WX_TEXT; // default to TEXT
    if (oflags & O_NOINHERIT)            wxflags |= WX_DONTINHERIT;

    if ((unsupp = oflags & ~(
                    O_BINARY|O_TEXT|O_APPEND|
                    O_TRUNC|O_EXCL|O_CREAT|
                    O_RDWR|O_WRONLY|O_TEMPORARY|
                    O_NOINHERIT|
                    O_SEQUENTIAL|O_RANDOM|O_SHORT_LIVED|
                    O_WTEXT|O_U16TEXT|O_U8TEXT
                    ))) {
        TRACE(":unsupported oflags 0x%x\n", unsupp);
    }
    return wxflags;
}

// return -1 on fail and set errno
int open(const char* file, int flags, ...)
{
    FileSystemCode_t code;
    stdio_handle_t*  object;
    UUId_t           handle;
    int              pmode = 0;
    int              fd    = -1;
    va_list          ap;

    if (!file) {
        _set_errno(EINVAL);
        return -1;
    }

    // Extract pmode flags
    if (flags & O_CREAT) {
        va_start(ap, flags);
        pmode = va_arg(ap, int);
        va_end(ap);
    }
    
    // Try to open the file by directly communicating with the file-service
    code = OpenFile(file, _fopts(flags), _faccess(flags), &handle);
    if (!_fval(code)) {
        if (stdio_handle_create(-1, oftowx((unsigned int)flags), &object)) {
            CloseFile(handle);
            return -1;
        }
        stdio_handle_set_handle(object, handle);
        stdio_handle_set_ops_type(object, STDIO_HANDLE_FILE);
        
        fd = object->fd;
    }
    return fd;
}
