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
#include <os/handle.h>
#include <os/mollenos.h>

int
close(
    _In_ int fd)
{
    stdio_handle_t* handle;
    int             options = 0;
    oserr_t         status;

    handle = stdio_handle_get(fd);
    if (!handle) {
        _set_errno(EBADFD);
        return -1;
    }

    // Shared descriptors use a registered handle across local aliases. A
    // failed final release keeps the descriptor available for a close retry;
    // in particular, a failed SHM unmap must not discard the attachment state.
    if (handle->Shared) {
        status = OSHandleRelease(&handle->OSHandle);
        if (status != OS_EOK) {
            return OsErrToErrNo(status);
        }
        stdio_handle_delete(handle);
        return 0;
    }

    // Persistent descriptors must not release their operation context, so
    // only request a full close for other descriptors.
    if (!(handle->XTFlags & __IO_PERSISTANT)) {
        options |= STDIO_CLOSE_FULL;
    }

    // If the implementation has a close method, we invoke that to
    // do any neccessary cleanup.
    if (handle->Ops->close) {
        handle->Ops->close(handle, options);
    }

    // If an OS handle is in use, we free that for the underlying system.
    if (handle->OSHandle.ID != UUID_INVALID) {
        OSHandleDestroy(&handle->OSHandle);
    }
    stdio_handle_delete(handle);
    return 0;
}
