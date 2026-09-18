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

#ifndef __OS_MEMORY_H__
#define __OS_MEMORY_H__

#include <os/types/memory.h>

_CODE_BEGIN

/**
 * @brief Allocates a virtual memory region with the requested size and flags.
 * @param Hint Preferred address for the allocation, or NULL for no preference.
 * @param Length Number of bytes to allocate.
 * @param Flags Memory protection and allocation flags.
 * @param MemoryOut Receives the base address of the allocated region.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
MemoryAllocate(
        _In_  void*        Hint,
        _In_  size_t       Length,
        _In_  unsigned int Flags,
        _Out_ void**       MemoryOut));

/**
 * @brief Releases a previously allocated virtual memory region.
 * @param Memory Base address of the region to release.
 * @param Length Size of the region in bytes.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
MemoryFree(
        _In_ void*  Memory,
        _In_ size_t Length));

/**
 * @brief Changes the protection flags of a virtual memory region.
 * @param Memory Base address of the region to protect.
 * @param Length Size of the region in bytes.
 * @param Flags New memory protection flags.
 * @param PreviousFlags Receives the previous protection flags.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
MemoryProtect(
        _In_  void*         Memory,
        _In_  size_t        Length,
        _In_  unsigned int  Flags,
        _Out_ unsigned int* PreviousFlags));

/**
 * @brief Retrieves the allocation descriptor for an address.
 * @param Memory Address within the allocation to query.
 * @param DescriptorOut Receives the allocation descriptor.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
MemoryQueryAllocation(
        _In_ void*                 Memory,
        _In_ OSMemoryDescriptor_t* DescriptorOut));

/**
 * @brief Retrieves memory attributes for a range of addresses.
 * @param Memory Base address of the range to query.
 * @param Length Size of the range in bytes.
 * @param AttributeArray Receives the attributes for the queried range.
 * @return OS_EOK on success; otherwise, an error code.
 */
CRTDECL(oserr_t,
MemoryQueryAttributes(
        _In_ void*         Memory,
        _In_ size_t        Length,
        _In_ unsigned int* AttributeArray));

/**
 * @brief Returns the page-size for the current platform.
 * @return The page size in bytes.
 */
CRTDECL(size_t,
MemoryPageSize(void));

_CODE_END
#endif //!__OS_MEMORY_H__
