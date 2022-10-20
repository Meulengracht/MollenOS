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
    oserr_t oserr;

    oserr = DmaAttach(handle, attachment);
    if (oserr != OsOK) {
        return oserr;
    }

    oserr = DmaAttachmentMap(attachment, 0);
    if (oserr != OsOK) {
        DmaDetach(attachment);
        return oserr;
    }
    return OsOK;
}

oserr_t VFSNodeWrite(struct VFSRequest* request, size_t* writtenOut)
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

    nodeVfs = handle->Node->FileSystem;
    if (nodeVfs->Interface->Operations.Write == NULL) {
        // Write is not a required operation for the filesystem to support,
        // so it may not be supported.
        oserr = OsNotSupported;
        goto cleanup;
    }

    oserr = __MapUserBuffer(request->parameters.transfer.bufferHandle, &attachment);
    if (oserr != OsOK) {
        goto cleanup;
    }

    usched_rwlock_r_lock(&handle->Node->Lock);
    oserr = nodeVfs->Interface->Operations.Write(
            nodeVfs->Data, handle->Data,
            attachment.handle, attachment.buffer,
            request->parameters.transfer.offset,
            request->parameters.transfer.length,
            writtenOut
    );
    usched_rwlock_r_unlock(&handle->Node->Lock);
    if (oserr == OsOK) {
        handle->Mode     = MODE_WRITE;
        handle->Position += *writtenOut;
        if (handle->Position > handle->Node->Stats.Size) {
            handle->Node->Stats.Size = handle->Position;
        }
    }

    oserr2 = DmaDetach(&attachment);
    if (oserr2 != OsOK) {
        WARNING("VFSNodeWrite failed to detach read buffer");
    }

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}

oserr_t VFSNodeWriteAt(uuid_t fileHandle, UInteger64_t* position, uuid_t bufferHandle, size_t offset, size_t length, size_t* writtenOut)
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
            position->QuadPart, &result.QuadPart
    );
    if (oserr != OsOK) {
        goto unmap;
    }
    handle->Position = result.QuadPart;

    oserr = nodeVfs->Interface->Operations.Write(
            nodeVfs->Data, handle->Data,
            attachment.handle, attachment.buffer,
            offset, length, writtenOut
    );
    if (oserr == OsOK) {
        handle->Mode     = MODE_WRITE;
        handle->Position += *writtenOut;
        if (handle->Position > handle->Node->Stats.Size) {
            handle->Node->Stats.Size = handle->Position;
        }
    }

unmap:
    usched_rwlock_r_unlock(&handle->Node->Lock);
    oserr2 = DmaDetach(&attachment);
    if (oserr2 != OsOK) {
        WARNING("VFSNodeWriteAt failed to detach read buffer");
    }

cleanup:
    VFSNodeHandlePut(handle);
    return oserr;
}
