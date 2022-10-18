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

static oserr_t __MapUserBuffer(uuid_t handle, DMAAttachment_t* attachment)
{
    oserr_t osStatus;

    osStatus = DmaAttach(handle, attachment);
    if (osStatus != OsOK) {
        return osStatus;
    }

    osStatus = DmaAttachmentMap(attachment, DMA_ACCESS_WRITE);
    if (osStatus != OsOK) {
        DmaDetach(attachment);
        return osStatus;
    }
    return OsOK;
}

oserr_t VFSNodeRead(struct VFSRequest* request, size_t* readOut)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               oserr, oserr2;
    DMAAttachment_t       attachment;

    oserr = VFSNodeHandleGet(request->parameters.transfer.fileHandle, &handle);
    if (oserr != OsOK) {
        return oserr;
    }

    // The regular read and write operations we will only support for files. Directories
    // and symlinks have their own respective interfaces for dealing with Read/Write.
    if (!__NodeIsFile(handle->Node)) {
        oserr = OsNotSupported;
        goto cleanup;
    }

    oserr = __MapUserBuffer(request->parameters.transfer.bufferHandle, &attachment);
    if (oserr != OsOK) {
        goto cleanup;
    }

    nodeVfs = handle->Node->FileSystem;

    usched_rwlock_r_lock(&handle->Node->Lock);
    oserr = nodeVfs->Interface->Operations.Read(
            nodeVfs->Data, handle->Data,
            attachment.handle, attachment.buffer,
            request->parameters.transfer.offset,
            request->parameters.transfer.length,
            readOut);
    usched_rwlock_r_unlock(&handle->Node->Lock);
    if (oserr == OsOK) {
        handle->Mode     = MODE_READ;
        handle->Position += *readOut;
    }

    oserr2 = DmaDetach(&attachment);
    if (oserr2 != OsOK) {
        WARNING("VFSNodeRead failed to detach read buffer");
    }

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}

oserr_t VFSNodeReadAt(uuid_t fileHandle, UInteger64_t* position, uuid_t bufferHandle, size_t offset, size_t length, size_t* readOut)
{
    struct VFSNodeHandle* handle;
    struct VFS*           nodeVfs;
    oserr_t               oserr, oserr2;
    DMAAttachment_t       attachment;
    UInteger64_t          result;

    oserr = VFSNodeHandleGet(fileHandle, &handle);
    if (oserr != OsOK) {
        return oserr;
    }

    // The regular read and write operations we will only support for files. Directories
    // and symlinks have their own respective interfaces for dealing with Read/Write.
    if (!__NodeIsFile(handle->Node)) {
        oserr = OsNotSupported;
        goto cleanup;
    }

    oserr = __MapUserBuffer(bufferHandle, &attachment);
    if (oserr != OsOK) {
        goto cleanup;
    }

    nodeVfs = handle->Node->FileSystem;

    usched_rwlock_r_lock(&handle->Node->Lock);
    oserr = nodeVfs->Interface->Operations.Seek(
            nodeVfs->Data, handle->Data,
            position->QuadPart, &result.QuadPart);
    if (oserr != OsOK) {
        goto unmap;
    }
    handle->Position = result.QuadPart;

    oserr = nodeVfs->Interface->Operations.Read(
            nodeVfs->Data, handle->Data,
            attachment.handle, attachment.buffer,
            offset, length, readOut
    );
    if (oserr == OsOK) {
        handle->Mode     = MODE_READ;
        handle->Position += *readOut;
    }

unmap:
    usched_rwlock_r_unlock(&handle->Node->Lock);
    oserr2 = DmaDetach(&attachment);
    if (oserr2 != OsOK) {
        WARNING("VFSNodeReadAt failed to detach read buffer");
    }

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}
