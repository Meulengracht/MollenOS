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
 */

#ifndef __OS_DMABUF_H__
#define __OS_DMABUF_H__

#include <os/osdefs.h>
#include <os/types/dma.h>

_CODE_BEGIN
/**
 * Creates a new page aligned dma buffer and provides the initial attachment.
 * The attachment will already be mapped into current address space and provided mappings.
 * @param info       [In] The information related to the buffer that should be created.
 * @param attachment [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaCreate(
        _In_ DMABuffer_t*     info,
        _In_ DMAAttachment_t* attachment));

/**
 * Exports the dma buffer provided. The structure must be prefilled with most
 * of the information before being passed.
 * @param buffer     [In] Information about the buffer that should be exported by the kernel.
 * @param info       [In] Options related to the export of the buffer.
 * @param attachment [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaExport(
        _In_ void*                   buffer,
        _In_ DMABuffer_t*     info,
        _In_ DMAAttachment_t* attachment));

/**
 * Attach to a dma buffer handle, but does not perform further actions.
 * @param handle     [In] The dma buffer handle to attach to.
 * @param attachment [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaAttach(
        _In_ uuid_t           handle,
        _In_ DMAAttachment_t* attachment));

/**
 * Should be called both by attachers and the creator when the memory
 * dma buffer should be released. The dma regions are not released before all attachers have detachted.
 * @param attachment [In] The dma buffer to detach from.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaDetach(
        _In_ DMAAttachment_t* attachment));

/**
 * Map the dma buffer into current memory space and get the metrics of the buffer
 * @param attachment  [In] The dma buffer attachment to map into memory space.
 * @param accessFlags [In] The memory access flags the mapping should be created with.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaAttachmentMap(
        _In_ DMAAttachment_t* attachment,
        _In_ unsigned int     accessFlags));

/**
 * Commits the address by allocating physical page to backup the virtual address
 * @param attachment [In] The dma buffer attachment that should be resized.
 * @param address    [In] The starting virtual address to commit from.
 * @param length     [In] The number of bytes to commit (will be rounded up to page-size).
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaAttachmentCommit(
        _In_ DMAAttachment_t* attachment,
        _In_ vaddr_t          address,
        _In_ size_t           length));

/**
 * Resizes the dma buffer to the given length argument. This must be within
 * the provided capacity, otherwise the call will fail.
 * @param attachment [In] The dma buffer attachment that should be resized.
 * @param length     [In] The new length of the buffer attachment segment.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaAttachmentResize(
        _In_ DMAAttachment_t* attachment,
        _In_ size_t           length));

/**
 * Used by the attachers to refresh their memory mappings of the provided dma buffer.
 * @param attachment [In] The dma buffer attachment mapping that should be refreshed.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaAttachmentRefresh(
        _In_ DMAAttachment_t* attachment));

/**
 * Remove the mapping that has been previously created by its counterpart.
 * @param attachment [In] The dma buffer attachment to unmap from current addressing space.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaAttachmentUnmap(
        _In_ DMAAttachment_t* attachment));

/**
 * Call this once with the count parameter to get the number of
 * scatter-gather entries, then the second time with the dma_sg parameter
 * to retrieve a list of all the entries
 * @param attachment [In]  Attachment to the dma buffer to query the list of dma entries
 * @param sg_table   [Out] Pointer to storage for the sg_table. This must be manually freed.
 * @param max_count  [In]  Max number of entries, if 0 or below it will get all number of entries.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaGetSGTable(
        _In_ DMAAttachment_t* attachment,
        _In_ DMASGTable_t*    sgTable,
        _In_ int              maxCount));

/**
 * Converts a virtual buffer offset into a dma_sg index + offset
 * @param sgTable  [In]  Scatter-gather table to perform the lookup in.
 * @param offset    [In]  The offset that should be converted to a sg-index/offset 
 * @param sg_index  [Out] A pointer to the variable for the index.
 * @param sg_offset [Out] A pointer to the variable for the offset.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
DmaSGTableOffset(
        _In_  DMASGTable_t* sgTable,
        _In_  size_t        offset,
        _Out_ int*          sgIndex,
        _Out_ size_t*       sgOffset));

_CODE_END

#endif //!__OS_DMABUF_H__
