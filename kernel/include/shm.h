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

#ifndef __SHM_H__
#define __SHM_H__

#include <os/types/shm.h>

/**
 * @brief Create a new page-aligned memory region that stretches over <Capacity>. The
 * entire region is only committed directly to memory for <Length> bytes.
 * @param shm           [In]  Configuration details of the new shared memory buffer.
 * @param kernelMapping [Out] The allocated virtual buffer address for the kernel mapping.
 * @param userMapping   [Out] The allocated virtual buffer address for the user mapping.
 * @param handleOut     [Out] The global handle for the memory region.
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMCreate(
        _In_  SHM_t*  shm,
        _Out_ void**  kernelMapping,
        _Out_ void**  userMapping,
        _Out_ uuid_t* handleOut);

/**
 * @brief Exports an existing memory region that stretches over <Length>. Makes sure
 * all the memory from <Memory> to <Memory + Length> is committed.
 * @param memory [In]  The buffer that should be exported.
 * @param size   [In]  Length of the buffer.
 * @param flags  [In]  Configuration of the memory region and behaviour.
 * @param Handle [Out] The global handle for the memory region.
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMExport(
        _In_  void*        memory,
        _In_  size_t       size,
        _In_  unsigned int flags,
        _In_  unsigned int accessFlags,
        _Out_ uuid_t*      handleOut);

/**
 * @brief
 * @param shmID
 * @param sizeOut
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMAttach(
        _In_ uuid_t  shmID,
        _In_ size_t* sizeOut);

/**
 * @brief
 * @param regionHandle
 * @param memoryOut
 * @param sizeOut
 * @param accessFlags
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMMap(
        _In_ uuid_t       shmID,
        _In_ size_t       offset,
        _In_ size_t       length,
        _In_ unsigned int flags,
        _In_ SHMHandle_t* handle);

/**
 *
 * @param handle
 * @param memory
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMUnmap(
        _In_ uuid_t handle,
        _In_ void*  memory);

/**
 * Commits a certain area of a memory region.
 * @param handle     [In]
 * @param memoryBase [In]
 * @param memory     [In]
 * @param length     [In]
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMCommit(
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
KERNELAPI oserr_t KERNELABI
SHMRead(
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
KERNELAPI oserr_t KERNELABI
SHMWrite(
        _In_  uuid_t      Handle,
        _In_  size_t      Offset,
        _In_  const void* Buffer,
        _In_  size_t      Length,
        _Out_ size_t*     BytesWritten);

/**
 * Retrieves a scatter gather list of the physical memory blocks for the given memory region.
 * @param handle
 * @param sgCountOut
 * @param sgOut
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMBuildSG(
        _In_  uuid_t   handle,
        _Out_ int*     sgCountOut,
        _Out_ SHMSG_t* sgOut);

/**
 * Retrieves the kernel memory mapping for a given memory region handle
 * @param handle    The handle of the memory region
 * @param bufferOut A pointer to the buffer will be set if successful.
 * @return          The status of the operation.
 */
KERNELAPI oserr_t KERNELABI
SHMKernelMapping(
        _In_  uuid_t handle,
        _Out_ void** bufferOut);

#endif //!__SHM_H__
