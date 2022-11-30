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
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeRead(uuid_t fileHandle, uuid_t bufferHandle, size_t offset, size_t length, size_t* readOut)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               oserr;

    oserr = VFSNodeHandleGet(fileHandle, &handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // The regular read and write operations we will only support for files. Directories
    // and symlinks have their own respective interfaces for dealing with Read/Write.
    if (!__NodeIsFile(handle->Node)) {
        oserr = OS_ENOTSUPPORTED;
        goto cleanup;
    }

    nodeVfs = handle->Node->FileSystem;

    usched_rwlock_r_lock(&handle->Node->Lock);
    oserr = nodeVfs->Interface->Operations.Read(
            nodeVfs->Interface, nodeVfs->Data, handle->Data,
            bufferHandle,
            NULL, /* No buffer can be supplied on this code path */
            offset,
            length,
            readOut);
    usched_rwlock_r_unlock(&handle->Node->Lock);
    if (oserr == OS_EOK) {
        handle->Mode     = MODE_READ;
        handle->Position += *readOut;
    }

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}

oserr_t VFSNodeReadAt(uuid_t fileHandle, UInteger64_t* position, uuid_t bufferHandle, size_t offset, size_t length, size_t* readOut)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               oserr;
    UInteger64_t          result;

    oserr = VFSNodeHandleGet(fileHandle, &handle);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // The regular read and write operations we will only support for files. Directories
    // and symlinks have their own respective interfaces for dealing with Read/Write.
    if (!__NodeIsFile(handle->Node)) {
        oserr = OS_ENOTSUPPORTED;
        goto cleanup;
    }

    nodeVfs = handle->Node->FileSystem;

            NULL, /* No buffer can be supplied on this code path */
    usched_rwlock_r_lock(&handle->Node->Lock);
    oserr = nodeVfs->Interface->Operations.Seek(
            nodeVfs->Interface,
            nodeVfs->Data,
            handle->Data,
            position->QuadPart,
            &result.QuadPart
    );
    if (oserr != OS_EOK) {
        goto unlock;
    }
    handle->Position = result.QuadPart;

    oserr = nodeVfs->Interface->Operations.Read(
            nodeVfs->Interface,
            nodeVfs->Data,
            handle->Data,
            bufferHandle,
            NULL, /* No buffer can be supplied on this code path */
            offset, length, readOut
    );
    if (oserr == OS_EOK) {
        handle->Mode     = MODE_READ;
        handle->Position += *readOut;
    }

unlock:
    usched_rwlock_r_unlock(&handle->Node->Lock);

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}
