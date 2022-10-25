/**
 * MollenOS
 *
 * Copyright 2020, Philip Meulengracht
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
 *
 * Generic IO Cotntol. These are functions that may be supported by all
 * io descriptors. If they are not supported errno is set to EBADF
 */

#include <errno.h>
#include <internal/_io.h>
#include <ioctl.h>
#include <os/mollenos.h>

int ioctl(int iod, unsigned long request, ...)
{
    stdio_handle_t* handle = stdio_handle_get(iod);
    oserr_t      status;
    va_list         args;
    
    if (!handle) {
        _set_errno(EBADF);
        return -1;
    }

    va_start(args, request);
    status = handle->ops.ioctl(handle, request, args);
    va_end(args);

    if (status == OS_ENOTSUPPORTED) {
        _set_errno(EBADF);
        return -1;
    }
    return OsErrToErrNo(status);
}
