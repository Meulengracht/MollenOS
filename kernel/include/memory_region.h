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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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
 * MemoryRegionCreate
 * * Create a new page-aligned memory region that stretches over <Capacity>. The
 * * entire region is only committed directly to memory for <Length> bytes.
 * @param Length        [In]  The number of bytes that should be committed initially.
 * @param Capacity      [In]  The number of bytes that we should reserve for continuity.
 * @param Flags         [In]  Configuration of the memory region and behaviour.
 * @param KernelMapping [Out] The allocated virtual buffer address for the kernel mapping.
 * @param UserMapping   [Out] The allocated virtual buffer address for the user mapping.
 * @param Handle        [Out] The global handle for the memory region. 
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionCreate(
    _In_  size_t  Length,
    _In_  size_t  Capacity,
    _In_  Flags_t Flags,
    _Out_ void**  KernelMapping,
    _Out_ void**  UserMapping,
    _Out_ UUId_t* Handle);

/**
 * MemoryRegionCreateExisting
 * * Exports an existing memory region that stretches over <Length>. Makes sure
 * * all the memory from <Memory> to <Memory + Length> is committed.
 * @param Memory   [In]  The buffer that should be exported.
 * @param Length   [In]  Length of the buffer.
 * @param Flags    [In]  Configuration of the memory region and behaviour.
 * @param Handle   [Out] The global handle for the memory region. 
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionCreateExisting(
    _In_  void*   Memory,
    _In_  size_t  Length,
    _In_  Flags_t Flags,
    _Out_ UUId_t* HandleOut);

/**
 * MemoryRegionAttach
 * * Refreshes the current memory mapping to align with the memory region.
 * * This is neccessary for all users of an memory region that has been resized.
 * @param Handle [In]
 * @param Length [Out]
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionAttach(
    _In_  UUId_t  Handle,
    _Out_ size_t* Length);

/**
 * MemoryRegionInherit
 * * Refreshes the current memory mapping to align with the memory region.
 * * This is neccessary for all users of an memory region that has been resized.
 * @param Handle        [In]
 * @param Memory        [In]
 * @param CurrentLength [In]
 * @param NewLength     [Out]
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionInherit(
    _In_  UUId_t  Handle,
    _Out_ void**  Memory,
    _Out_ size_t* Length);

/**
 * MemoryRegionUnherit
 * * Refreshes the current memory mapping to align with the memory region.
 * * This is neccessary for all users of an memory region that has been resized.
 * @param Handle        [In]
 * @param Memory        [In]
 * @param CurrentLength [In]
 * @param NewLength     [Out]
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionUnherit(
    _In_ UUId_t Handle,
    _In_ void*  Memory);

/**
 * MemoryRegionResize
 * * Refreshes the current memory mapping to align with the memory region.
 * * This is neccessary for all users of an memory region that has been resized.
 * @param Handle        [In]
 * @param Memory        [In]
 * @param CurrentLength [In]
 * @param NewLength     [Out]
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionResize(
    _In_ UUId_t Handle,
    _In_ void*  Memory,
    _In_ size_t NewLength);

/**
 * MemoryRegionRefresh
 * * Refreshes the current memory mapping to align with the memory region.
 * * This is neccessary for all users of an memory region that has been resized.
 * @param Handle        [In]
 * @param Memory        [In]
 * @param CurrentLength [In]
 * @param NewLength     [Out]
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionRefresh(
    _In_  UUId_t  Handle,
    _In_  void*   Memory,
    _In_  size_t  CurrentLength,
    _Out_ size_t* NewLength);

/**
 * MemoryRegionRead
 * * Refreshes the current memory mapping to align with the memory region.
 * * This is neccessary for all users of an memory region that has been resized.
 * @param Handle        [In]
 * @param Memory        [In]
 * @param CurrentLength [In]
 * @param NewLength     [Out]
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionRead(
    _In_  UUId_t  Handle,
    _In_  size_t  Offset,
    _In_  void*   Buffer,
    _In_  size_t  Length,
    _Out_ size_t* BytesRead);

/**
 * MemoryRegionWrite
 * * Refreshes the current memory mapping to align with the memory region.
 * * This is neccessary for all users of an memory region that has been resized.
 * @param Handle        [In]
 * @param Memory        [In]
 * @param CurrentLength [In]
 * @param NewLength     [Out]
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionWrite(
    _In_  UUId_t      Handle,
    _In_  size_t      Offset,
    _In_  const void* Buffer,
    _In_  size_t      Length,
    _Out_ size_t*     BytesWritten);

/**
 * MemoryRegionGetSg
 * * Refreshes the current memory mapping to align with the memory region.
 * * This is neccessary for all users of an memory region that has been resized.
 * @param Handle        [In]
 * @param Memory        [In]
 * @param CurrentLength [In]
 * @param NewLength     [Out]
 */
KERNELAPI OsStatus_t KERNELABI
MemoryRegionGetSg(
    _In_  UUId_t         Handle,
    _Out_ int*           SgCountOut,
    _Out_ struct dma_sg* SgListOut);

#endif //!__MEMORY_REGION_H__
