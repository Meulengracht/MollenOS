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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * DMA-BUFFER Definitions & Structures
 * - This header describes the dmabuf-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <internal/_syscalls.h>
#include <os/dmabuf.h>

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
    _In_ struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return Syscall_DmaAttachmentMap(attachment);
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
dma_get_metrics(
    _In_  struct dma_attachment* attachment,
    _Out_ int*                   count,
    _Out_ struct dma_sg*         sg_list)
{
    if (!attachment || !count) {
        return OsInvalidParameters;
    }
    return Syscall_DmaGetMetrics(attachment, count, sg_list);
}
