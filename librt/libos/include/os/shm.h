/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __OS_SHM_H__
#define __OS_SHM_H__

#include <os/types/shm.h>
#include <os/types/handle.h>

_CODE_BEGIN
/**
 * Creates a new page aligned memory buffer and provides the initial attachment.
 * The attachment will already be mapped into current address space and provided mappings.
 * @param shm    [In] The information related to the buffer that should be created.
 * @param handle [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMCreate(
        _In_  SHM_t*      shm,
        _Out_ OSHandle_t* handleOut));

/**
 * Exports the memory buffer provided. The structure must be prefilled with most
 * of the information before being passed.
 * @param buffer [In] The buffer that should be wrapped in a SHM instance. The buffer
 *                    must be page-aligned, otherwise the exporting will fail.
 * @param shm    [In] Options related to the export of the buffer.
 * @param handle [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMExport(
        _In_  void*       buffer,
        _In_  SHM_t*      shm,
        _Out_ OSHandle_t* handleOut));

/**
 * @brief Creates a conformed buffer clone from a source buffer. The buffer will automatically
 * be filled on creation, or the source will be filled on conformed buffer destruction. Doing this
 * will automatically map the two (worst-case) buffers into the callers memory space.
 * OBS: This operation combines the functionality of an Attach/Map and because of it's memory
 * requirements should only be used scarcely, preferably by driver stacks that need to do this.
 * @param handle     The source SHM handle ID.
 * @param conformity The memory conformity requirements of the buffer.
 * @param flags      How the behaviour of the conformed buffer should be.
 * @param handleOut  The new handle of the conformed (or original) buffer.
 * @return OS_EOK on successful creation or import of the buffer.
 *         OS_EOOM if a buffer could not be allocated due to memory constraints.
 */
CRTDECL(oserr_t,
SHMConform(
        _In_ uuid_t                  shmID,
        _In_ enum OSMemoryConformity conformity,
        _In_ unsigned int            flags,
        _In_ OSHandle_t*             handleOut));

/**
 * Attach to a memory buffer handle, but does not perform further actions.
 * @param shmID  [In] The memory buffer handle to attach to.
 * @param handle [In] The structure to fill with the attachment information.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMAttach(
        _In_  uuid_t      shmID,
        _Out_ OSHandle_t* handleOut));

/**
 * Map into or update an existing memory buffer in the current memory space. When re-mapping an already
 * existing SHMHandle object, the buffer pointer may change and thus invalidating the current mapping that
 * SHMHandle_t.Buffer is pointing to. Only when extending or shrinking a mapping does SHMHandle_t.Buffer
 * not change. When moving (when offset != initial offset) a mapping, the existing buffer will be destroyed
 * and a new one will be provided.
 * If the access flags change from the original mapping, the entire mapping will get the new flags applied.
 * @param handle [In] The memory buffer attachment to map.
 * @param offset [In] The offset into the buffer the mapping should start.
 * @param length [In] The length of the buffer that should be mapped.
 * @param flags  [In] The memory access flags the mapping should be created with. A buffer originally created
 *                    with read access, cannot be mapped with any other access than read.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMMap(
        _In_ OSHandle_t*  handle,
        _In_ size_t       offset,
        _In_ size_t       length,
        _In_ unsigned int flags));

/**
 * Commits the address by allocating physical page to backup the virtual address
 * @param handle  [In] The memory buffer attachment that should be resized.
 * @param address [In] The starting virtual address to commit from.
 * @param length  [In] The number of bytes to commit (will be rounded up to page-size).
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMCommit(
        _In_ OSHandle_t* handle,
        _In_ vaddr_t     address,
        _In_ size_t      length));

/**
 * Remove the mapping that has been previously created by its counterpart.
 * @param handle [In] The memory buffer attachment to unmap from current addressing space.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMUnmap(
        _In_ OSHandle_t* handle,
        _In_ void*       address,
        _In_ size_t      length));

/**
 * @brief
 * @param handle
 * @return
 */
CRTDECL(void*,
SHMBuffer(
        _In_ OSHandle_t* handle));

/**
 * @brief
 * @param handle
 * @return
 */
CRTDECL(size_t,
SHMBufferLength(
        _In_ OSHandle_t* handle));

/**
 * @brief
 * @param handle
 * @return
 */
CRTDECL(size_t,
SHMBufferCapacity(
        _In_ OSHandle_t* handle));

/**
 * Call this once with the count parameter to get the number of
 * scatter-gather entries, then the second time with the memory_sg parameter
 * to retrieve a list of all the entries
 * @param handle [In]  Attachment to the memory buffer to query the list of memory entries
 * @param sg_table   [Out] Pointer to storage for the sg_table. This must be manually freed.
 * @param max_count  [In]  Max number of entries, if 0 or below it will get all number of entries.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMGetSGTable(
        _In_ OSHandle_t*   handle,
        _In_ SHMSGTable_t* sgTable,
        _In_ int           maxCount));

/**
 * Converts a virtual buffer offset into a memory_sg index + offset
 * @param sgTable  [In]  Scatter-gather table to perform the lookup in.
 * @param offset    [In]  The offset that should be converted to a sg-index/offset 
 * @param sg_index  [Out] A pointer to the variable for the index.
 * @param sg_offset [Out] A pointer to the variable for the offset.
 * @return Status of the operation.
 */
CRTDECL(oserr_t,
SHMSGTableOffset(
        _In_  SHMSGTable_t* sgTable,
        _In_  size_t        offset,
        _Out_ int*          sgIndex,
        _Out_ size_t*       sgOffset));

_CODE_END
#endif //!__OS_SHM_H__
