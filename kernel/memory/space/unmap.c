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

#include <arch/mmu.h>
#include <debug.h>
#include <heap.h>
#include <machine.h>
#include "private.h"
#include "../shm/range.h"

static oserr_t
__ClearPhysicalPages(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         size)
{
    paddr_t* addresses;
    oserr_t  oserr;
    int      pageCount;
    int      pagesCleared = 0;
    int      pagesFreed = 0;
    TRACE("__ClearPhysicalPages(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          memorySpace, address, size);

    // Allocate the buffer for physical page addresses.
    pageCount = DIVUP(size, GetMemorySpacePageSize());
    addresses = kmalloc(sizeof(paddr_t) * pageCount);
    if (!addresses) {
        oserr = OS_EOOM;
        goto exit;
    }

    // Clear page table entries before freeing the physical pages.
    oserr = ArchMmuClearVirtualPages(
            memorySpace,
            address,
            pageCount,
            &addresses[0],
            &pagesFreed,
            &pagesCleared
    );
    if (pagesCleared) {
        // Flush stale translations on all CPUs before these pages are reused.
        MSSync(memorySpace, address, size);
        
        // Free the physical pages.
        if (pagesFreed) {
            FreePhysicalMemory(pagesFreed, &addresses[0]);
        }
    }
    kfree(addresses);

exit:
    TRACE("__ClearPhysicalPages returns=%u", oserr);
    return oserr;
}

static oserr_t
__ClearSHMPages(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         length)
{
    size_t       pageSize = GetMemorySpacePageSize();
    int          remaining = (int)(length / pageSize);
    int          cleared;
    int          freed;
    unsigned int attributes;
    oserr_t      status;
    oserr_t      queryStatus;

    while (remaining != 0) {
        status = ArchMmuClearVirtualPages(memorySpace, address, remaining, NULL, &freed, &cleared);
        assert(freed == 0);
        if (cleared != 0) {
            MSSync(memorySpace, address, (size_t)cleared * pageSize);
            address += (size_t)cleared * pageSize;
            remaining -= cleared;
        }
        if (status == OS_EOK) {
            return OS_EOK;
        }
        if (status != OS_ENOENT && status != OS_EINCOMPLETE) {
            return status;
        }
        if (remaining == 0) {
            return status;
        }
        // A previous partial unmap or failed map may leave empty slots.
        // Skip only an absent page, never a failed removal of a live mapping.
        attributes = 0;
        queryStatus = GetMemorySpaceAttributes(memorySpace, address, pageSize, &attributes);
        if (queryStatus != OS_ENOENT && (queryStatus != OS_EOK || attributes != 0)) {
            return status;
        }
        address += pageSize;
        remaining--;
    }
    return OS_EOK;
}

oserr_t
MSUnmap(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         length)
{
    struct MSContext*    context;
    struct MSAllocation* allocation;
    vaddr_t              startAddress = address;
    oserr_t              oserr;
    bool                 incomplete = false;
    bool                 cleared = false;
    size_t               pageSize = GetMemorySpacePageSize();
    struct MSAllocation* restricted;
    bool                 allowFreedPages;

    TRACE("MemorySpaceUnmap(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          memorySpace, address, size);

    if (memorySpace == NULL) {
        return OS_EINVALPARAMS;
    }

    context = __MSContextGet(memorySpace);
    
    allowFreedPages = true;
    oserr = MSAllocationFindSHM(
        context,
        address,
        length,
        allowFreedPages,
        &restricted
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (restricted != NULL) {
        if (address % pageSize != 0 || length > SIZE_MAX - (pageSize - 1)) {
            return OS_EINVALPARAMS;
        }

        length = DIVUP(length, pageSize) * pageSize;
        if (!__SHMRangeContains(restricted->Address, restricted->Length, address, length)) {
            return OS_EINVALPARAMS;
        }

        if (restricted->References != 1) {
            return OS_ELINKS;
        }

        // Clear PTEs and synchronize before releasing the allocation's view.
        oserr = __ClearSHMPages(memorySpace, address, length);
        if (oserr != OS_EOK) {
            return oserr;
        }
        cleared = true;
    }

    allocation = MSAllocationLookup(context, address);
    if (allocation != NULL) {
        struct MSAllocation* original;

        startAddress = allocation->Address;
        oserr = MSAllocationFree(
            context,
            address,
            length,
            &original
        );
        if (oserr != OS_EOK) {
            // Linked allocations still use the physical pages and cannot be freed.
            // An incomplete free can continue with unmapping the requested range.
            if (oserr != OS_EINCOMPLETE) {
                return oserr;
            }
            incomplete = true;
        }
    }

    // Skip physical-page cleanup when the SHM path already cleared the mapping.
    oserr = cleared ? OS_EOK : __ClearPhysicalPages(memorySpace, address, length);
    if (oserr != OS_EOK) {
        WARNING("__FreeMapping failed to clear underlying pages!!");
        return oserr;
    }

    if (incomplete) {
        // Keep the virtual region until the entire allocation has been freed.
        // Report the partial free to the caller.
        oserr = OS_EINCOMPLETE;
        goto exit;
    }

    // On a complete free, use the allocation's start address to release
    // the full virtual region.
    if (context != NULL && DynamicMemoryPoolContains(&context->Heap, startAddress)) {
        DynamicMemoryPoolFree(&context->Heap, startAddress);
    } else if (StaticMemoryPoolContains(&GetMachine()->GlobalAccessMemory, startAddress)) {
        StaticMemoryPoolFree(&GetMachine()->GlobalAccessMemory, startAddress);
    } else if (DynamicMemoryPoolContains(&memorySpace->ThreadMemory, startAddress)) {
        DynamicMemoryPoolFree(&memorySpace->ThreadMemory, startAddress);
    }

exit:
    return oserr;
}

oserr_t
MemorySpaceUnmap(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t address,
    _In_ size_t length)
{
    struct MSContext* context;
    oserr_t           status;

    if (memorySpace == NULL) {
        return OS_EINVALPARAMS;
    }
    
    // Serialize changes against user copies in every space in the group.
    context = __MSContextGet(memorySpace);
    if (context != NULL) {
        MutexLock(&context->UserCopyLock);
    }
    
    status = MSUnmap(memorySpace, address, length);
    
    if (context != NULL) {
        MutexUnlock(&context->UserCopyLock);
    }
    return status;
}
