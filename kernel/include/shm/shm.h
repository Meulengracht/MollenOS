/**
 * Copyright, Philip Meulengracht
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
 * @brief Create a page-aligned memory region of <Capacity> bytes.
 * Only <Length> bytes are committed directly to memory.
 * @param shm           [In]  Configuration details of the new shared memory buffer.
 * @param handleOut     [Out] The global handle for the memory region.
 * @return Status of the operation
 */
KERNELAPI oserr_t KERNELABI
SHMCreate(
    _In_ SHM_t*       shm,
    _In_ SHMHandle_t* handle);

/**
 * @brief Create a zeroed device buffer from reserved system memory.
 *
 * The pages are physically contiguous. The physical start address is a multiple
 * of alignment, and the first size bytes do not cross a multiple of boundary.
 * Each mapping is cached or uncached as required by the pool. The buffer is
 * mapped into the calling process. Releasing it returns the pages to the pool.
 *
 * @param size Nonzero number of bytes, a multiple of the page size.
 * @param alignment Zero or a power of two, measured in physical addresses.
 * @param boundary Zero or a power of two no smaller than size.
 * @param handle Receives the buffer handle, including the CPU pointer.
 * @return OS_EOK on success. OS_EINVALPARAMS for invalid arguments.
 *         OS_ENOTSUPPORTED if the system has no device pool. OS_EOOM if the
 *         pool has no fitting run.
 */
KERNELAPI oserr_t KERNELABI
SHMCreateDeviceBuffer(
    _In_  size_t       size,
    _In_  size_t       alignment,
    _In_  size_t       boundary,
    _Out_ SHMHandle_t* handle);

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
 * @brief Release a CPU attachment. On failure, leave unfinished stages for retry.
 * Other attachments and device mappings have separate references. Once this
 * attachment's reference is released, that step is complete even if the storage
 * is still in use.
 * 
 * @param handle Attachment updated after each completed stage. Retry on failure.
 * @return OS_EOK if released or already empty, or a cleanup error.
 */
KERNELAPI oserr_t KERNELABI
SHMDetach(
    _InOut_ SHMHandle_t* handle);

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
 * Retrieves a scatter gather list of physical memory blocks for the given region.
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
 * Retrieves the kernel memory mapping for a given memory region handle.
 * @param handle    The handle of the memory region
 * @param bufferOut A pointer to the buffer will be set if successful.
 * @return          The status of the operation.
 */
KERNELAPI oserr_t KERNELABI
SHMKernelMapping(
    _In_  uuid_t handle,
    _Out_ void** bufferOut);

/**
 * @brief Validate a caller-granted IPC buffer and add one global reference.
 * The caller must have a security context.
 * Release the reference with ReleaseGlobalHandle after using the mapping.
 * @param handleID IPC buffer accessible through the caller's grant table.
 * @param bufferOut Receives the initialized kernel mapping on success.
 * @return OS_EOK, or an error without adding a reference or changing bufferOut.
 */
KERNELAPI oserr_t KERNELABI
SHMAcquireIPCBufferReference(
    _In_  uuid_t  handleID,
    _Out_ void** bufferOut);

/**
 * @brief Find a globally addressed IPC buffer and add one global reference.
 * No caller grant is required or added. Release the reference with
 * ReleaseGlobalHandle after using the mapping.
 * @param globalHandleID Global address of the destination IPC buffer.
 * @param bufferOut Receives the initialized kernel mapping on success.
 * @return OS_EOK, or an error without adding a reference or changing bufferOut.
 */
KERNELAPI oserr_t KERNELABI
SHMAcquireGlobalIPCBuffer(
    _In_  uuid_t  globalHandleID,
    _Out_ void** bufferOut);

#endif //!__SHM_H__
