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

OsStatus_t
dma_create(
    _In_ struct dma_buffer_info* info,
    _In_ struct dma_attachment*  attachment)
{
    if (!info || !attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaCreate(info, attachment);
}

OsStatus_t
dma_export(
    _In_ void*                   buffer,
    _In_ struct dma_buffer_info* info,
    _In_ struct dma_attachment*  attachment)
{
    if (!buffer || !info || !attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaExport(buffer, info, attachment);
}

OsStatus_t
dma_attach(
    _In_ UUId_t                 handle,
    _In_ struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttach(handle, attachment);
}

OsStatus_t
dma_attachment_map(
    _In_ struct dma_attachment* attachment,
    _In_ unsigned int           accessFlags)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentMap(attachment, accessFlags);
}

OsStatus_t
dma_attachment_resize(
    _In_ struct dma_attachment* attachment,
    _In_ size_t                 length)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentResize(attachment, length);
}

OsStatus_t
dma_attachment_refresh_map(
    struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentRefresh(attachment);
}

OsStatus_t
dma_attachment_map_commit(
        _In_ struct dma_attachment* attachment,
        _In_ vaddr_t                address,
        _In_ size_t                 length)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentCommit(attachment, address, length);
}

OsStatus_t
dma_attachment_unmap(
    struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentUnmap(attachment);
}

OsStatus_t
dma_detach(
    struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaDetach(attachment);
}

OsStatus_t
dma_get_sg_table(
    _In_ struct dma_attachment* attachment,
    _In_ struct dma_sg_table*   sg_table,
    _In_ int                    max_count)
{
    OsStatus_t status;
    
    if (!attachment || !sg_table) {
        return OsInvalidParameters;
    }
    
    // get count unless provided, 
    // then allocate space and then retrieve full list
    sg_table->count = max_count;
    if (max_count <= 0) {
        status = Syscall_DmaGetMetrics(attachment->handle, &sg_table->count, NULL);
        if (status != OsOK) {
            return status;
        }
    }
    
    sg_table->entries = malloc(sizeof(struct dma_sg) * sg_table->count);
    if (!sg_table->entries) {
        return OsOutOfMemory;
    }
    return Syscall_DmaGetMetrics(attachment->handle, &sg_table->count, sg_table->entries);
}


OsStatus_t
dma_sg_table_offset(
    _In_  struct dma_sg_table* sg_table,
    _In_  size_t               offset,
    _Out_ int*                 sg_index_out,
    _Out_ size_t*              sg_offset_out)
{
    if (!sg_table || !sg_index_out || !sg_offset_out) {
        return OsInvalidParameters;
    }
    
    for (int i = 0; i < sg_table->count; i++) {
        if (offset < sg_table->entries[i].length) {
            *sg_index_out  = i;
            *sg_offset_out = offset;
            return OsOK;
        }
        offset -= sg_table->entries[i].length;
    }
    return OsInvalidParameters;
}
