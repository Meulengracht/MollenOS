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
 */

//#define __TRACE

#include <ddk/utils.h>
#include <errno.h>
#include <io.h>
#include <internal/_file.h>
#include <internal/_io.h>
#include <os/mollenos.h>
#include <string.h>

int write(int fd, const void* buffer, unsigned int length)
{
    stdio_handle_t* handle       = stdio_handle_get(fd);
    size_t          bytesWritten = 0;
    int             res;
    oserr_t      status;
    TRACE("write(fd=%i, buffer=0x%" PRIxIN ", length=%u)", fd, buffer, length);

    // Don't write uneven bytes in case of UTF8/16
    if ((handle->wxflag & WX_UTF) == WX_UTF && (length & 1)) {
        _set_errno(EINVAL);
        res = -1;
        goto exit;
    }

    // If appending, go to EOF
    if (handle->wxflag & WX_APPEND) {
        lseek(fd, 0, SEEK_END);
    }

    // If we aren't in text mode, raw write the data without any text-processing
    status = handle->ops.write(handle, (char*)buffer, length, &bytesWritten);
    if (status != OS_EOK) {
        res = OsErrToErrNo(status);
    } else {
        res = (int)bytesWritten;
    }

exit:
    TRACE("write return=%i (errno %i)", res, errno);
    return res;
}
