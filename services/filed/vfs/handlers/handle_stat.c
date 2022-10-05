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
#include <string.h>
#include <vfs/requests.h>
#include <vfs/storage.h>
#include <vfs/vfs.h>
#include "../private.h"

oserr_t VFSNodeGetPosition(struct VFSRequest* request, uint64_t* positionOut)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    *positionOut = handle->Position;

    VFSNodeHandlePut(handle);
    return OsOK;
}

oserr_t VFSNodeGetAccess(struct VFSRequest* request, uint32_t* accessKindOut)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    *accessKindOut = handle->AccessKind;

    VFSNodeHandlePut(handle);
    return OsOK;
}

oserr_t VFSNodeSetAccess(struct VFSRequest* request)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // So in order to change access kind for this node, we have to check new permissions versus
    // old, if the new are more restrictive, we need to make sure it doesn't coincide with
    // the current ones.
    // TODO: implement support for this

    VFSNodeHandlePut(handle);
    return OsOK;
}

oserr_t VFSNodeGetSize(struct VFSRequest* request, uint64_t* sizeOut)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    *sizeOut = handle->Node->Stats.Size;
    usched_rwlock_r_unlock(&handle->Node->Lock);

    VFSNodeHandlePut(handle);
    return OsOK;
}

oserr_t VFSNodeSetSize(struct VFSRequest* request)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;
    UInteger64_t          size;

    size.u.LowPart  = request->parameters.set_size.size_low;
    size.u.HighPart = request->parameters.set_size.size_high;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_w_lock(&handle->Node->Lock);
    osStatus = handle->Node->FileSystem->Interface->Operations.Truncate(
            handle->Node->FileSystem->Data, handle->Data, size.QuadPart);
    if (osStatus == OsOK) {
        handle->Node->Stats.Size = size.QuadPart;
    }
    usched_rwlock_w_unlock(&handle->Node->Lock);
    VFSNodeHandlePut(handle);
    return osStatus;
}

oserr_t VFSNodeStatHandle(uuid_t fileHandle, struct VFSStat* stat)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    memcpy(stat, &handle->Node->Stats, sizeof(struct VFSStat));
    usched_rwlock_r_unlock(&handle->Node->Lock);

    VFSNodeHandlePut(handle);
    return OsOK;
}

oserr_t VFSNodeStatFsHandle(struct VFSRequest* request, struct VFSStatFS* stat)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    osStatus = handle->Node->FileSystem->Interface->Operations.Stat(
            handle->Node->FileSystem->Data, stat);
    usched_rwlock_r_unlock(&handle->Node->Lock);

    VFSNodeHandlePut(handle);
    return osStatus;
}

oserr_t VFSNodeStatStorageHandle(struct VFSRequest* request, StorageDescriptor_t* stat)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    memcpy(
            stat,
           &handle->Node->FileSystem->Storage->Stats,
           sizeof(StorageDescriptor_t)
   );
    usched_rwlock_r_unlock(&handle->Node->Lock);
    VFSNodeHandlePut(handle);
    return OsOK;
}

oserr_t VFSNodeGetPathHandle(uuid_t handleID, mstring_t** pathOut)
{
    struct VFSNodeHandle* handle;
    oserr_t               osStatus;

    osStatus = VFSNodeHandleGet(handleID, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    *pathOut = VFSNodeMakePath(handle->Node, 0);
    usched_rwlock_r_unlock(&handle->Node->Lock);
    VFSNodeHandlePut(handle);
    return OsOK;
}
