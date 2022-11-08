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
 *
 */

#include <ddk/utils.h>
#include <vfs/requests.h>
#include <vfs/vfs.h>
#include "../private.h"

static oserr_t __FlushHandle(struct VFSNodeHandle* handle)
{
    // TODO Implement
    return OS_EOK;
}

oserr_t VFSNodeSeek(struct VFSRequest* request, uint64_t* positionOut)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               oserr;
    UInteger64_t          position, result;

    position.u.LowPart  = request->parameters.seek.position_low;
    position.u.HighPart = request->parameters.seek.position_high;

    oserr = VFSNodeHandleGet(request->parameters.seek.fileHandle, &handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // We support seeking in files and directories (exceptionally). However there is a catch
    // for directories. You can only seek to ith entry, not byte offset. So if you try to seek
    // out of the directory, it will fail
    if (!__NodeIsFile(handle->Node) && !__NodeIsDirectory(handle->Node)) {
        oserr = OS_ENOTSUPPORTED;
        goto cleanup;
    }

    if (__NodeIsDirectory(handle->Node)) {
        // When seeking in directories only the low-part of the position
        // will be used. Ignore the high-part.
        if (position.u.LowPart >= handle->Node->Children.element_count) {
            oserr = OS_EINVALPARAMS;
            goto cleanup;
        }

        // Otherwise update position
        handle->Position = position.u.LowPart;
        *positionOut = position.u.LowPart;
        oserr = OS_EOK;
        goto cleanup;
    }

    // If we go here, then we can assume we are seeking in a file. This will require
    // us to go interact with the underlying FS, as seeking means different things.
    nodeVfs = handle->Node->FileSystem;

    usched_rwlock_r_lock(&handle->Node->Lock);
    if (handle->Mode == MODE_WRITE) {
        oserr = __FlushHandle(handle);
        if (oserr != OS_EOK) {
            usched_rwlock_r_unlock(&handle->Node->Lock);
            goto cleanup;
        }
    }

    oserr = nodeVfs->Interface->Operations.Seek(
            nodeVfs->Interface,
            nodeVfs->Data,
            handle->Data,
            position.QuadPart,
            &result.QuadPart
    );
    if (oserr == OS_EOK) {
        handle->Mode     = MODE_NONE;
        handle->Position = result.QuadPart;
    }
    usched_rwlock_r_unlock(&handle->Node->Lock);

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}
