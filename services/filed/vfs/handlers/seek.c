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
    return OsOK;
}

oserr_t VFSNodeSeek(struct VFSRequest* request, uint64_t* positionOut)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               osStatus;
    UInteger64_t          position, result;

    position.u.LowPart  = request->parameters.seek.position_low;
    position.u.HighPart = request->parameters.seek.position_high;

    osStatus = VFSNodeHandleGet(request->parameters.seek.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    nodeVfs = handle->Node->FileSystem;

    usched_rwlock_r_lock(&handle->Node->Lock);
    if (handle->Mode == MODE_WRITE) {
        osStatus = __FlushHandle(handle);
        if (osStatus != OsOK) {
            goto cleanup;
        }
    }

    osStatus = nodeVfs->Interface->Operations.Seek(
            nodeVfs->CommonData, handle->Data,
            position.QuadPart, &result.QuadPart);
    if (osStatus == OsOK) {
        handle->Mode     = MODE_NONE;
        handle->Position = result.QuadPart;
    }

cleanup:
    usched_rwlock_r_unlock(&handle->Node->Lock);
    VFSNodeHandlePut(handle);
    return osStatus;
}
