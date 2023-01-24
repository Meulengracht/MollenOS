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

static oserr_t
__FreeMapping(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         length)
{
    struct MSAllocation* link;
    MemorySpace_t*       ms = memorySpace;
    vaddr_t              startAddress = address;
    size_t               allocSize = length;
    do {
        oserr_t oserr = MSAllocationFree(
                ms->Context,
                &startAddress,
                &allocSize,
                &link
        );
        if (oserr != OS_EOK) {
            if (oserr == OS_EINCOMPLETE) {
                // the allocation is still being used
                return OS_EOK;
            }
            return oserr;
        }

        // We managed to free the allocation, now free the underlying
        // pages
        oserr = __ClearPhysicalPages(
                ms,
                startAddress + allocSize,
                length
        );
        WARNING_IF(oserr != OS_EOK,
                   "__FreeMapping failed to clear underlying pages!!");

        // Update iterator members
        if (link) {
            WARNING_IF(link->MemorySpace != memorySpace,
                       "__FreeMapping cross memory-space freeing!!! DANGEROUS!!");
            ms = link->MemorySpace;
            startAddress = link->Address;
            allocSize = link->Length;
        }
    } while (link != NULL);
    return OS_EOK;
}

oserr_t
MemorySpaceUnmap(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size)
{
    struct MSAllocation* allocation;
    vaddr_t              startAddress = address;
    size_t               actualSize = size;
    oserr_t              oserr;

    TRACE("MemorySpaceUnmap(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          memorySpace, address, size);

    if (memorySpace == NULL || size == 0 || address == 0) {
        return OS_EINVALPARAMS;
    }

    allocation = MSAllocationLookup(memorySpace->Context, address);
    if (allocation != NULL) {
        startAddress = allocation->Address;
        if (size != allocation->Length) {
            WARNING("MemorySpaceUnmap: cannot do partial free of %" PRIuIN "/%" PRIuIN "bytes, correcting to full length");
            actualSize = allocation->Length;
        }
    }

    oserr = __FreeMapping(memorySpace, startAddress, actualSize);
    if (oserr != OS_EOK) {
        goto exit;
    }

    // Free the range in either GAM or Process memory
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
