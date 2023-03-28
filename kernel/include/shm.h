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
 * @param handleOut     [Out] The global handle for the memory region.
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMCreate(
        _In_ SHM_t*       shm,
        _In_ SHMHandle_t* handle);

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
        _In_  SHMHandle_t* handle);

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
KERNELAPI oserr_t KERNELABI
SHMConform(
        _In_ uuid_t                  shmID,
        _In_ enum OSMemoryConformity conformity,
        _In_ unsigned int            flags,
        _In_ unsigned int            access,
        _In_ size_t                  offset,
        _In_ size_t                  length,
        _In_ SHMHandle_t*            handle);

/**
 * @brief
 * @param shmID
 * @param handle
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMAttach(
        _In_ uuid_t       shmID,
        _In_ SHMHandle_t* handle);

/**
 * @brief
 * @param handle
 * @return
 */
KERNELAPI oserr_t KERNELABI
SHMDetach(
        _In_ SHMHandle_t* handle);

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
        _In_ SHMHandle_t* handle,
        _In_ size_t       offset,
        _In_ size_t       length,
        _In_ unsigned int flags);

/**
 *
 * @param handle
 * @param memory
 * @param address
 * @param length
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMUnmap(
        _In_ SHMHandle_t* handle,
        _In_ vaddr_t      address,
        _In_ size_t       length);

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
 * Retrieves a scatter gather list of the physical memory block
 * s for the given memory region.
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
