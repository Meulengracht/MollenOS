/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Memory Region Interface
 * - Implementation of shared memory buffers between kernel and processes. This
 *   can be used for conveniently transfering memory.
 */

#ifndef __MEMORY_REGION_H__
#define __MEMORY_REGION_H__

#include <os/osdefs.h>

struct dma_sg;

/**
 * Create a new page-aligned memory region that stretches over <Capacity>. The
 * entire region is only committed directly to memory for <Length> bytes.
 * @param length        [In]  The number of bytes that should be committed initially.
 * @param capacity      [In]  The number of bytes that we should reserve for continuity.
 * @param flags         [In]  Configuration of the memory region and behaviour.
 * @param pageMask      [In]  The accepted pagemask for any physical pages allocated.
 * @param kernelMapping [Out] The allocated virtual buffer address for the kernel mapping.
 * @param userMapping   [Out] The allocated virtual buffer address for the user mapping.
 * @param handleOut     [Out] The global handle for the memory region.
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionCreate(
        _In_  size_t       length,
        _In_  size_t       capacity,
        _In_  unsigned int flags,
        _In_  size_t       pageMask,
        _Out_ void**       kernelMapping,
        _Out_ void**       userMapping,
        _Out_ uuid_t*      handleOut);

/**
 * Exports an existing memory region that stretches over <Length>. Makes sure
 * all the memory from <Memory> to <Memory + Length> is committed.
 * @param memory [In]  The buffer that should be exported.
 * @param size   [In]  Length of the buffer.
 * @param flags  [In]  Configuration of the memory region and behaviour.
 * @param Handle [Out] The global handle for the memory region.
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionCreateExisting(
        _In_  void*   memory,
        _In_  size_t  size,
        _In_  unsigned int flags,
        _Out_ uuid_t* handleOut);

/**
 *
 * @param Handle
 * @param Length
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionAttach(
        _In_  uuid_t  Handle,
        _Out_ size_t* Length);

/**
 *
 * @param regionHandle
 * @param memoryOut
 * @param sizeOut
 * @param accessFlags
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionInherit(
        _In_  uuid_t       regionHandle,
        _Out_ void**       memoryOut,
        _Out_ size_t*      sizeOut,
        _In_  unsigned int accessFlags);

/**
 *
 * @param handle
 * @param memory
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionUnherit(
        _In_ uuid_t handle,
        _In_ void*  memory);

/**
 * Resizes the committed memory portion of the memory region. This automatically
 * commits any holes in the memory region.
 * @param handle
 * @param memory
 * @param newLength
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionResize(
        _In_ uuid_t handle,
        _In_ void*  memory,
        _In_ size_t newLength);

/**
 * Refreshes the current memory mapping to align with the memory region.
 * This is neccessary for all users of an memory region that has been resized.
 * @param handle        [In]
 * @param memory        [In]
 * @param currentLength [In]
 * @param newLength     [Out]
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionRefresh(
        _In_  uuid_t  handle,
        _In_  void*   memory,
        _In_  size_t  currentLength,
        _Out_ size_t* newLength);

/**
 * Commits a certain area of a memory region.
 * @param handle     [In]
 * @param memoryBase [In]
 * @param memory     [In]
 * @param length     [In]
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionCommit(
        _In_ uuid_t handle,
        _In_ void*  memoryBase,
        _In_ void*  memory,
        _In_ size_t length);

/**
 * Performs a DMA read from the memory region
 * @param Handle
 * @param Offset
 * @param Buffer
 * @param Length
 * @param BytesRead
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionRead(
        _In_  uuid_t  Handle,
        _In_  size_t  Offset,
        _In_  void*   Buffer,
        _In_  size_t  Length,
        _Out_ size_t* BytesRead);

/**
 * Performs a DMA write to the memory region
 * @param Handle
 * @param Offset
 * @param Buffer
 * @param Length
 * @param BytesWritten
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionWrite(
        _In_  uuid_t      Handle,
        _In_  size_t      Offset,
        _In_  const void* Buffer,
        _In_  size_t      Length,
        _Out_ size_t*     BytesWritten);

/**
 * Retrieves a scatter gather list of the physical memory blocks for the given memory region.
 * @param handle
 * @param sgCountOut
 * @param sgListOut
 * @return Status of the operation
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionGetSg(
        _In_  uuid_t         handle,
        _Out_ int*           sgCountOut,
        _Out_ struct dma_sg* sgListOut);

/**
 * Retrieves the kernel memory mapping for a given memory region handle
 * @param handle    The handle of the memory region
 * @param bufferOut A pointer to the buffer will be set if successful.
 * @return          The status of the operation.
 */
KERNELAPI oscode_t KERNELABI
MemoryRegionGetKernelMapping(
        _In_  uuid_t handle,
        _Out_ void** bufferOut);

#endif //!__MEMORY_REGION_H__
