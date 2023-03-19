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

    // allocate memory for the physical pages
    pageCount = DIVUP(size, GetMemorySpacePageSize());
    addresses = kmalloc(sizeof(paddr_t) * pageCount);
    if (!addresses) {
        oserr = OS_EOOM;
        goto exit;
    }

    // Free the underlying resources first, before freeing the upper resources
    oserr = ArchMmuClearVirtualPages(
            memorySpace,
            address,
            pageCount,
            &addresses[0],
            &pagesFreed,
            &pagesCleared
    );
    if (pagesCleared) {
        // free the physical memory
        if (pagesFreed) {
            FreePhysicalMemory(pagesFreed, &addresses[0]);
        }
        MSSync(memorySpace, address, size);
    }
    kfree(addresses);

exit:
    TRACE("__ClearPhysicalPages returns=%u", oserr);
    return oserr;
}

oserr_t
MemorySpaceUnmap(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         length)
{
    struct MSAllocation* allocation;
    vaddr_t              startAddress = address;
    oserr_t              oserr;
    bool                 incomplete = false;

    TRACE("MemorySpaceUnmap(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          memorySpace, address, size);

    if (memorySpace == NULL) {
        return OS_EINVALPARAMS;
    }

    allocation = MSAllocationLookup(memorySpace->Context, address);
    if (allocation != NULL) {
        struct MSAllocation* original;
        startAddress = allocation->Address;

        oserr = MSAllocationFree(
                memorySpace->Context,
                address,
                length,
                &original
        );
        if (oserr != OS_EOK) {
            // There are two reasons for this to fail, either there are links
            // which means we *cannot* free this memory, as the physical memory
            // is still being used, or the free was incomplete. Incomplete frees
            // act exactly like
            if (oserr != OS_EINCOMPLETE) {
                return oserr;
            }
            incomplete = true;
        }
    }

    // We managed to free the allocation, now free the underlying
    // pages
    oserr = __ClearPhysicalPages(memorySpace, address, length);
    if (oserr != OS_EOK) {
        WARNING("__FreeMapping failed to clear underlying pages!!");
        return oserr;
    }

    if (incomplete) {
        // In the case of an incomplete free, we cannot free the virtual region
        // just yet. We have to wait for a full free to occur for the region. Make sure
        // we proxy the information that a full free did not occur
        oserr = OS_EINCOMPLETE;
        goto exit;
    }

    // When a full free occurs, we must always use the start address of the allocation
    // and it's full length when freeing the virtual region.
    if (memorySpace->Context != NULL && DynamicMemoryPoolContains(&memorySpace->Context->Heap, startAddress)) {
        DynamicMemoryPoolFree(&memorySpace->Context->Heap, startAddress);
    } else if (StaticMemoryPoolContains(&GetMachine()->GlobalAccessMemory, startAddress)) {
        StaticMemoryPoolFree(&GetMachine()->GlobalAccessMemory, startAddress);
    } else if (DynamicMemoryPoolContains(&memorySpace->ThreadMemory, startAddress)) {
        DynamicMemoryPoolFree(&memorySpace->ThreadMemory, startAddress);
    }

exit:
    return oserr;
}
