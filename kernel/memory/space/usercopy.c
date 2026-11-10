/**
 * MollenOS
 *
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
 *
 *
 * Memory Space Interface
 * - Copying between kernel memory and userspace memory. The user range and
 *   every page in it are checked before anything is copied.
 */

#include <arch/mmu.h>
#include <machine.h>
#include "private.h"

static bool
__IsUserCopyRange(
    _In_ uintptr_t address,
    _In_ size_t    length)
{
    SystemMemoryMap_t*  map = &GetMachine()->MemoryMap;
    SystemMemoryRange_t ranges[] = { map->UserCode, map->UserHeap, map->ThreadLocal };
    size_t              offset;
    size_t              available;
    size_t              index;

    // The ThreadLocal length is the last valid offset, while code and heap store
    // a byte count. Adjust it so all three ranges can be checked the same way.
    ranges[2].Length++;
    while (length != 0) {
        available = 0;
        
        for (index = 0; index < 3; index++) {
            if (address < ranges[index].Start) {
                continue;
            }
            
            offset = address - ranges[index].Start;
            if (offset < ranges[index].Length) {
                available = ranges[index].Length - offset;
                break;
            }
        }

        if (available == 0) {
            return false;
        }
        
        if (length <= available) {
            return true;
        }

        // The buffer is allowed to continue into the next user region, as long
        // as there is no gap between the two.
        address += available;
        length -= available;
    }
    return true;
}

static oserr_t
__ValidateUserCopyPages(
    _In_ MemorySpace_t*       space,
    _In_ struct MSAllocation* allocation,
    _In_ uintptr_t            pageAddress,
    _In_ size_t               firstPageIndex,
    _In_ size_t               pageCount,
    _In_ bool                 toUser)
{
    size_t       pageSize = GetMemorySpacePageSize();
    size_t       index;
    unsigned int attributes;
    oserr_t      status;

    for (index = 0; index < pageCount; index++) {
        // A set bit in the allocation bitmap means the page has been freed, so
        // it must not be copied.
        if (allocation != NULL && bitmap_bits_set(&allocation->Pages,
                (int)(firstPageIndex + index), 1)) {
            return OS_EINVALPARAMS;
        }

        attributes = 0;
        status = GetMemorySpaceAttributes(
                space,
                pageAddress + index * pageSize,
                pageSize,
                &attributes
        );
        if (status != OS_EOK) {
            if (status == OS_ENOENT) {
                // Report missing pages as invalid parameters.
                return OS_EINVALPARAMS;
            }
            return status;
        }

        // Only committed userspace RAM can be copied. Guard pages and device
        // memory are rejected.
        if ((attributes & (MAPPING_USERSPACE | MAPPING_COMMIT)) != (MAPPING_USERSPACE | MAPPING_COMMIT) ||
            (attributes & (MAPPING_TRAPPAGE | MAPPING_DEVICE))) {
            return OS_EINVALPARAMS;
        }

        // Reject read-only mappings when writing to the user buffer.
        if (toUser && (attributes & MAPPING_READONLY)) {
            return OS_EINVALPARAMS;
        }
    }
    return OS_EOK;
}


static oserr_t
__CopyUser(
    _Out_ void*       destination,
    _In_  const void* source,
    _In_  size_t      length,
    _In_  bool        toUser)
{
    MemorySpace_t*       space = GetCurrentMemorySpace();
    struct MSContext*    context = __MSContextGet(space);
    struct MSAllocation* allocation;
    uintptr_t            address = (uintptr_t)(toUser ? destination : source);
    size_t               pageSize = GetMemorySpacePageSize();
    size_t               offset = address % pageSize;
    size_t               allocationOffset;
    size_t               pageCount;
    oserr_t              status = OS_EINVALPARAMS;

    if (destination == NULL || source == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }

    if (context == NULL || length - 1 > UINTPTR_MAX - address) {
        return OS_EINVALPARAMS;
    }
    
    if (!__IsUserCopyRange(address, length)) {
        return OS_EINVALPARAMS;
    }

    // All map, unmap and protection changes in this group take this lock as
    // well. We hold it until the last user access is done, which also covers
    // thread pages that are not tracked by an allocation.
    MutexLock(&context->UserCopyLock);
    allocation = MSAllocationAcquire(context, address);
    if (allocation != NULL) {
        if (!(allocation->Flags & MAPPING_USERSPACE) || address < allocation->Address) {
            goto exit;
        }
        
        allocationOffset = address - allocation->Address;
        if (allocationOffset >= allocation->Length) {
            goto exit;
        }

        if (length > allocation->Length - allocationOffset) {
            goto exit;
        }
    }

    // The last byte was checked above, so length - 1 + offset cannot overflow.
    // We do not round the length up, as that could go past UINTPTR_MAX.
    pageCount = (length - 1 + offset) / pageSize + 1;
    if (pageCount > INT32_MAX) {
        goto exit;
    }

    status = __ValidateUserCopyPages(
        space,
        allocation,
        address - offset,
        allocation != NULL ? (address - offset - allocation->Address) / pageSize : 0,
        pageCount,
        toUser
    );
    if (status != OS_EOK) {
        goto exit;
    }

    // Copy through the active mappings, so the RAM keeps its original cache
    // attributes. The architecture routines only touch the checked bytes, and
    // they never fault in pages.
    if (toUser) {
        ArchCopyToUser(destination, source, length);
    } else {
        ArchCopyFromUser(destination, source, length);
    }

exit:
    if (allocation != NULL) {
        MSAllocationRelease(context, allocation);
    }
    MutexUnlock(&context->UserCopyLock);
    return status;
}

oserr_t
MemorySpaceCopyFromUser(
    _Out_ void*       kernelBuffer,
    _In_  const void* userBuffer,
    _In_  size_t      length)
{
    bool toUser = false;
    return __CopyUser(kernelBuffer, userBuffer, length, toUser);
}

oserr_t
MemorySpaceCopyToUser(
    _Out_ void*       userBuffer,
    _In_  const void* kernelBuffer,
    _In_  size_t      length)
{
    bool toUser = true;
    return __CopyUser(userBuffer, kernelBuffer, length, toUser);
}
