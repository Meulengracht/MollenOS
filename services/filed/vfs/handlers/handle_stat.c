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
#include <vfs/vfs.h>
#include "../private.h"

oscode_t VFSNodeGetPosition(struct VFSRequest* request, uint64_t* positionOut)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    *positionOut = handle->Position;

    osStatus = VFSNodeHandlePut(handle);
    if (osStatus != OsOK) {
        WARNING("VFSNodeGetPosition failed to release handle lock");
    }
    return OsOK;
}

oscode_t VFSNodeGetAccess(struct VFSRequest* request, uint32_t* accessKindOut)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    *accessKindOut = handle->AccessKind;

    osStatus = VFSNodeHandlePut(handle);
    if (osStatus != OsOK) {
        WARNING("VFSNodeGetAccess failed to release handle lock");
    }
    return OsOK;
}

oscode_t VFSNodeSetAccess(struct VFSRequest* request)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    // So in order to change access kind for this node, we have to check new permissions versus
    // old, if the new are more restrictive, we need to make sure it doesn't coincide with
    // the current ones.
    // TODO: implement support for this

    osStatus = VFSNodeHandlePut(handle);
    if (osStatus != OsOK) {
        WARNING("VFSNodeGetAccess failed to release handle lock");
    }
    return OsOK;
}

oscode_t VFSNodeGetSize(struct VFSRequest* request, uint64_t* sizeOut)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    *sizeOut = handle->Node->Stats.Size;
    usched_rwlock_r_unlock(&handle->Node->Lock);

    osStatus = VFSNodeHandlePut(handle);
    if (osStatus != OsOK) {
        WARNING("VFSNodeGetSize failed to release handle lock");
    }
    return OsOK;
}

oscode_t VFSNodeSetSize(struct VFSRequest* request)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus, osStatus2;
    UInteger64_t       size;

    size.u.LowPart  = request->parameters.set_size.size_low;
    size.u.HighPart = request->parameters.set_size.size_high;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_w_lock(&handle->Node->Lock);
    osStatus = handle->Node->FileSystem->Module->Operations.Truncate(
            &handle->Node->FileSystem->Base, handle->Data, size.QuadPart);
    if (osStatus == OsOK) {
        handle->Node->Stats.Size = size.QuadPart;
    }
    usched_rwlock_w_unlock(&handle->Node->Lock);

    osStatus2 = VFSNodeHandlePut(handle);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeSetSize failed to release handle lock");
    }
    return osStatus;
}

oscode_t VFSNodeStatHandle(struct VFSRequest* request, struct VFSStat* stat)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    memcpy(stat, &handle->Node->Stats, sizeof(struct VFSStat));
    usched_rwlock_r_unlock(&handle->Node->Lock);

    osStatus = VFSNodeHandlePut(handle);
    if (osStatus != OsOK) {
        WARNING("VFSNodeStatHandle failed to release handle lock");
    }
    return OsOK;
}

oscode_t VFSNodeStatFsHandle(struct VFSRequest* request, struct VFSStatFS* stat)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus, osStatus2;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    osStatus = handle->Node->FileSystem->Module->Operations.Stat(
            &handle->Node->FileSystem->Base, stat);
    usched_rwlock_r_unlock(&handle->Node->Lock);

    osStatus2 = VFSNodeHandlePut(handle);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeStatFsHandle failed to release handle lock");
    }
    return osStatus;
}

oscode_t VFSNodeStatStorageHandle(struct VFSRequest* request, StorageDescriptor_t* stat)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    memcpy(stat, &handle->Node->FileSystem->Base.Storage, sizeof(StorageDescriptor_t));
    usched_rwlock_r_unlock(&handle->Node->Lock);

    osStatus = VFSNodeHandlePut(handle);
    if (osStatus != OsOK) {
        WARNING("VFSNodeStatStorageHandle failed to release handle lock");
    }
    return OsOK;
}

oscode_t VFSNodeGetPathHandle(struct VFSRequest* request, MString_t** pathOut)
{
    struct VFSNodeHandle* handle;
    oscode_t            osStatus;

    osStatus = VFSNodeHandleGet(request->parameters.get_position.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    usched_rwlock_r_unlock(&handle->Node->Lock);
    *pathOut = VFSNodeMakePath(handle->Node, 0);
    usched_rwlock_r_unlock(&handle->Node->Lock);

    osStatus = VFSNodeHandlePut(handle);
    if (osStatus != OsOK) {
        WARNING("VFSNodeGetPathHandle failed to release handle lock");
    }
    return OsOK;
}
