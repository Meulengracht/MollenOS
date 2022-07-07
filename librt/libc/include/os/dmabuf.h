
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

#ifndef __DMABUF_H__
#define __DMABUF_H__

#include <os/osdefs.h>

/**
 * Configuration flags for creation of a new dma buffer
 */
#define DMA_PERSISTANT  0x00000001U // Used to indicate underlying memory should not be freed upon dma destruction
#define DMA_UNCACHEABLE 0x00000002U // Used to indicate that the dma buffer should be disabled caching of the memory
#define DMA_CLEAN       0x00000004U // Zero out any allocated memory for the dma buffer
#define DMA_TRAP        0x00000008U // Dma region is a trap region. This can not be used in normal circumstances.

/**
 * Dma type buffer which can indicate which kind of memory will be allocated
 */
#define DMA_TYPE_REGULAR      0  // Regular allocation of physical memory
#define DMA_TYPE_DRIVER_ISA   1  // Memory should be located in ISA-compatable memory (<16mb)
#define DMA_TYPE_DRIVER_32LOW 2  // Memory should be allocated in a lower 32 bit region as device may not be fully 32 bit compliant
#define DMA_TYPE_DRIVER_32    3  // Memory should be located in 32 bit memory
#define DMA_TYPE_DRIVER_64    4  // Memory should be located in 64 bit memory

/**
 * Access flags that are available when mapping a dma buffer.
 * Read is always implied when mapping a region.
 */
#define DMA_ACCESS_WRITE   0x00000001U
#define DMA_ACCESS_EXECUTE 0x00000002U

struct dma_sg {
    uintptr_t address;
    size_t    length;
};

struct dma_sg_table {
    struct dma_sg* entries;
    int            count;
};

struct dma_buffer_info {
    const char*  name;
    size_t       length;
    size_t       capacity;
    unsigned int flags;
    unsigned int type;
};

struct dma_attachment {
    uuid_t handle;
    void*  buffer;
    size_t length;
};

_CODE_BEGIN
/**
 * Creates a new page aligned dma buffer and provides the initial attachment.
 * The attachment will already be mapped into current address space and provided mappings.
 * @param info       [In] The information related to the buffer that should be created.
 * @param attachment [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_create(struct dma_buffer_info* info, struct dma_attachment* attachment));

/**
 * Exports the dma buffer provided. The structure must be prefilled with most
 * of the information before being passed.
 * @param buffer     [In] Information about the buffer that should be exported by the kernel.
 * @param info       [In] Options related to the export of the buffer.
 * @param attachment [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_export(void* buffer, struct dma_buffer_info* info, struct dma_attachment* attachment));

/**
 * Attach to a dma buffer handle, but does not perform further actions.
 * @param handle     [In] The dma buffer handle to attach to.
 * @param attachment [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_attach(uuid_t handle, struct dma_attachment* attachment));

/**
 * Map the dma buffer into current memory space and get the metrics of the buffer
 * @param attachment  [In] The dma buffer attachment to map into memory space.
 * @param accessFlags [In] The memory access flags the mapping should be created with.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_attachment_map(struct dma_attachment* attachment, unsigned int accessFlags));

/**
 * Commits the address by allocating physical page to backup the virtual address
 * @param attachment [In] The dma buffer attachment that should be resized.
 * @param address    [In] The starting virtual address to commit from.
 * @param length     [In] The number of bytes to commit (will be rounded up to page-size).
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_attachment_map_commit(struct dma_attachment* attachment, vaddr_t address, size_t length));

/**
 * Resizes the dma buffer to the given length argument. This must be within
 * the provided capacity, otherwise the call will fail.
 * @param attachment [In] The dma buffer attachment that should be resized.
 * @param length     [In] The new length of the buffer attachment segment.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_attachment_resize(struct dma_attachment* attachment, size_t length));

/**
 * Used by the attachers to refresh their memory mappings of the provided dma buffer.
 * @param attachment [In] The dma buffer attachment mapping that should be refreshed.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_attachment_refresh_map(struct dma_attachment* attachment));

/**
 * Remove the mapping that has been previously created by its counterpart.
 * @param attachment [In] The dma buffer attachment to unmap from current addressing space.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_attachment_unmap(struct dma_attachment* attachment));

/**
 * Should be called both by attachers and the creator when the memory
 * dma buffer should be released. The dma regions are not released before all attachers have detachted.
 * @param attachment [In] The dma buffer to detach from.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_detach(struct dma_attachment* attachment));

/**
 * Call this once with the count parameter to get the number of
 * scatter-gather entries, then the second time with the dma_sg parameter
 * to retrieve a list of all the entries
 * @param attachment [In]  Attachment to the dma buffer to query the list of dma entries
 * @param sg_table   [Out] Pointer to storage for the sg_table. This must be manually freed.
 * @param max_count  [In]  Max number of entries, if 0 or below it will get all number of entries.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_get_sg_table(struct dma_attachment* attachment, struct dma_sg_table* sg_table, int max_count));

/**
 * Converts a virtual buffer offset into a dma_sg index + offset
 * @param sg_table  [In]  Scatter-gather table to perform the lookup in.
 * @param offset    [In]  The offset that should be converted to a sg-index/offset 
 * @param sg_index  [Out] A pointer to the variable for the index.
 * @param sg_offset [Out] A pointer to the variable for the offset.
 * @return Status of the operation.
 */
CRTDECL(oscode_t, dma_sg_table_offset(struct dma_sg_table* sg_table, size_t offset, int* sg_index, size_t* sg_offset));

_CODE_END

#endif //!__DMABUF_H__
