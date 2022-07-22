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

static oserr_t __MapUserBuffer(uuid_t handle, struct dma_attachment* attachment)
{
    oserr_t osStatus;

    osStatus = dma_attach(handle, attachment);
    if (osStatus != OsOK) {
        return osStatus;
    }

    osStatus = dma_attachment_map(attachment, DMA_ACCESS_WRITE);
    if (osStatus != OsOK) {
        dma_detach(attachment);
        return osStatus;
    }
    return OsOK;
}

oserr_t VFSNodeWrite(struct VFSRequest* request, size_t* writtenOut)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               osStatus, osStatus2;
    struct dma_attachment attachment;

    osStatus = VFSNodeHandleGet(request->parameters.transfer.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    osStatus = __MapUserBuffer(request->parameters.transfer.bufferHandle, &attachment);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    nodeVfs = handle->Node->FileSystem;

    usched_rwlock_r_lock(&handle->Node->Lock);
    osStatus = nodeVfs->Interface->Operations.Write(
            nodeVfs->CommonData, handle->Data,
            attachment.handle, attachment.buffer,
            request->parameters.transfer.offset,
            request->parameters.transfer.length,
            writtenOut);
    usched_rwlock_r_unlock(&handle->Node->Lock);
    if (osStatus == OsOK) {
        handle->Mode     = MODE_WRITE;
        handle->Position += *writtenOut;
    }

    osStatus2 = dma_detach(&attachment);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeWrite failed to detach read buffer");
    }

cleanup:
    VFSNodeHandlePut(handle);
    return osStatus;
}

oserr_t VFSNodeWriteAt(struct VFSRequest* request, size_t* writtenOut)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               osStatus, osStatus2;
    struct dma_attachment attachment;
    UInteger64_t          position, result;

    position.u.LowPart  = request->parameters.transfer_absolute.position_low;
    position.u.HighPart = request->parameters.transfer_absolute.position_high;

    osStatus = VFSNodeHandleGet(request->parameters.transfer.fileHandle, &handle);
    if (osStatus != OsOK) {
        return osStatus;
    }

    osStatus = __MapUserBuffer(request->parameters.transfer.bufferHandle, &attachment);
    if (osStatus != OsOK) {
        goto cleanup;
    }

    nodeVfs = handle->Node->FileSystem;

    usched_rwlock_r_lock(&handle->Node->Lock);
    osStatus = nodeVfs->Interface->Operations.Seek(
            nodeVfs->CommonData, handle->Data,
            position.QuadPart, &result.QuadPart);
    if (osStatus != OsOK) {
        goto unmap;
    }
    handle->Position = result.QuadPart;

    osStatus = nodeVfs->Interface->Operations.Write(
            nodeVfs->CommonData, handle->Data,
            attachment.handle, attachment.buffer,
            request->parameters.transfer.offset,
            request->parameters.transfer.length,
            writtenOut);
    if (osStatus == OsOK) {
        handle->Mode     = MODE_WRITE;
        handle->Position += *writtenOut;
    }

unmap:
    usched_rwlock_r_unlock(&handle->Node->Lock);
    osStatus2 = dma_detach(&attachment);
    if (osStatus2 != OsOK) {
        WARNING("VFSNodeWriteAt failed to detach read buffer");
    }

cleanup:
    VFSNodeHandlePut(handle);
    return osStatus;
}
