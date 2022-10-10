/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * DMA-BUFFER Definitions & Structures
 * - This header describes the dmabuf-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <os/dmabuf.h>
#include <stdlib.h>

oserr_t
DmaCreate(
    _In_ DMABuffer_t*     info,
    _In_ DMAAttachment_t* attachment)
{
    if (!info || !attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaCreate(info, attachment);
}

oserr_t
DmaExport(
    _In_ void*            buffer,
    _In_ DMABuffer_t*     info,
    _In_ DMAAttachment_t* attachment)
{
    if (!buffer || !info || !attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaExport(buffer, info, attachment);
}

oserr_t
DmaAttach(
        _In_ uuid_t           handle,
        _In_ DMAAttachment_t* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttach(handle, attachment);
}

oserr_t
DmaAttachmentMap(
    _In_ DMAAttachment_t* attachment,
    _In_ unsigned int     accessFlags)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentMap(attachment, accessFlags);
}

oserr_t
DmaAttachmentResize(
    _In_ DMAAttachment_t* attachment,
    _In_ size_t           length)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentResize(attachment, length);
}

oserr_t
DmaAttachmentRefresh(
    DMAAttachment_t* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentRefresh(attachment);
}

oserr_t
DmaAttachmentCommit(
        _In_ DMAAttachment_t* attachment,
        _In_ vaddr_t          address,
        _In_ size_t           length)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentCommit(attachment, address, length);
}

oserr_t
DmaAttachmentUnmap(
    DMAAttachment_t* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentUnmap(attachment);
}

oserr_t
DmaDetach(
    DMAAttachment_t* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaDetach(attachment);
}

oserr_t
DmaGetSGTable(
    _In_ DMAAttachment_t* attachment,
    _In_ DMASGTable_t*    sgTable,
    _In_ int              maxCount)
{
    oserr_t status;
    
    if (!attachment || !sgTable) {
        return OsInvalidParameters;
    }
    
    // get count unless provided, 
    // then allocate space and then retrieve full list
    sgTable->count = maxCount;
    if (maxCount <= 0) {
        status = Syscall_DmaGetMetrics(attachment->handle, &sgTable->count, NULL);
        if (status != OsOK) {
            return status;
        }
    }

    sgTable->entries = malloc(sizeof(DMASG_t) * sgTable->count);
    if (!sgTable->entries) {
        return OsOutOfMemory;
    }
    return Syscall_DmaGetMetrics(attachment->handle, &sgTable->count, sgTable->entries);
}


oserr_t
DmaSGTableOffset(
    _In_  DMASGTable_t* sgTable,
    _In_  size_t        offset,
    _Out_ int*          sgIndex,
    _Out_ size_t*       sgOffset)
{
    if (!sgTable || !sgIndex || !sgOffset) {
        return OsInvalidParameters;
    }
    
    for (int i = 0; i < sgTable->count; i++) {
        if (offset < sgTable->entries[i].length) {
            *sgIndex  = i;
            *sgOffset = offset;
            return OsOK;
        }
        offset -= sgTable->entries[i].length;
    }
    return OsInvalidParameters;
}
