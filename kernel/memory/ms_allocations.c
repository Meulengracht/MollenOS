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

#include <debug.h>
#include <heap.h>
#include "private.h"

oserr_t
MSAllocationCreate(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         length,
        _In_ unsigned int   flags)
{
    struct MSAllocation* allocation;
    oserr_t              oserr = OS_EOK;

    TRACE("MSAllocationCreate(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ", flags=0x%x)",
          memorySpace, address, length, flags);

    // We only support allocation tracking for spaces with context
    if (memorySpace->Context == NULL) {
        goto exit;
    }

    allocation = kmalloc(sizeof(struct MSAllocation));
    if (!allocation) {
        oserr = OS_EOOM;
        goto exit;
    }

    ELEMENT_INIT(&allocation->Header, 0, allocation);
    allocation->MemorySpace = memorySpace;
    allocation->Address     = address;
    allocation->Length      = length;
    allocation->Flags       = flags;
    allocation->References  = 1;
    allocation->CloneOf     = NULL;
    MSContextAddAllocation(memorySpace->Context, allocation);

exit:
    TRACE("MSAllocationCreate returns=%u", oserr);
    return oserr;
}

static struct MSAllocation*
__FindAllocation(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address)
{
    foreach(element, &context->Allocations) {
        struct MSAllocation* allocation = element->value;
        if (address >= allocation->Address && address < (allocation->Address + allocation->Length)) {
            return allocation;
        }
    }
    return NULL;
}

struct MSAllocation*
MSAllocationLookup(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address)
{
    struct MSAllocation* result;
    if (context == NULL) {
        return NULL;
    }

    MutexLock(&context->SyncObject);
    result = __FindAllocation(context, address);
    MutexUnlock(&context->SyncObject);
    return result;
}

struct MSAllocation*
MSAllocationAcquire(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address)
{
    struct MSAllocation* allocation;

    if (context == NULL) {
        return NULL;
    }

    MutexLock(&context->SyncObject);
    allocation = __FindAllocation(context, address);
    if (allocation) {
        allocation->References++;
    }
    MutexUnlock(&context->SyncObject);
    return allocation;
}

oserr_t
MSAllocationReduce(
        _In_ struct MSContext* context,
        _In_ vaddr_t           address,
        _In_ size_t            size)
{
    struct MSAllocation* allocation = NULL;
    size_t               storedSize = size;
    oserr_t              oserr;

    TRACE("__ReleaseAllocation(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          memorySpace, address, size);

    if (context == NULL) {
        return OS_EINVALPARAMS;
    }

    MutexLock(&context->SyncObject);
    allocation = __FindAllocation(context, address);
    if (allocation == NULL) {
        oserr = OS_ENOENT;
        goto exit;
    }

    // If other callers are using the same memory, for instance when cloned,
    // then we must not neccesarily free the actual allocation.
    allocation->References--;
    if (allocation->References) {
        oserr = OS_EOK;
        goto exit;
    }

    // We support reductions, but only shrinking. Callers can only expand and
    // shrink it at the end of the allocation. It does not matter what the actual
    // address is, as long as the address is inside the bounds of the allocation.
    if (size != allocation->Length) {
        // Let's reduce the mapping
    }

    // Support multiple references to an allocation, which means when we try to unmap physical pages
    // we would like to make sure noone actually references them anymore
    if (memorySpace->Context) {
        MutexLock(&memorySpace->Context->SyncObject);
        allocation = __FindAllocation(memorySpace, address);
        if (allocation) {
            allocation->References--;
            if (allocation->References) {
                // still has references so we just free virtual
                MutexUnlock(&memorySpace->Context->SyncObject);
                oserr = OS_EOK;
                goto exit;
            }

            storedSize = allocation->Length;
            list_remove(&memorySpace->Context->Allocations, &allocation->Header);
        }
        MutexUnlock(&memorySpace->Context->SyncObject);
    }

    // then clear original copy if there was any
    if (allocation) {
        if (allocation->CloneOf) {
            WARNING_IF(allocation->CloneOf->MemorySpace != memorySpace,
                       "__ReleaseAllocation cross memory-space freeing!!! DANGEROUS!!");

            MSAllocationReduce(context,
                                allocation->CloneOf->Address,
                                allocation->CloneOf->Length);
        }
        kfree(allocation);
    }

exit:
    MutexUnlock(&context->SyncObject);
    TRACE("__ReleaseAllocation returns=%u", oserr);
    return oserr;
}

void
__LinkAllocations(
        _In_ MemorySpace_t*                memorySpace,
        _In_ vaddr_t                       address,
        _In_ struct MemorySpaceAllocation* link)
{
    struct MemorySpaceAllocation* allocation;
    TRACE("__LinkAllocations(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ")",
          memorySpace, address);

    MutexLock(&memorySpace->Context->SyncObject);
    allocation = __FindAllocation(memorySpace, address);
    if (allocation) {
        allocation->CloneOf = link;
    }
    MutexUnlock(&memorySpace->Context->SyncObject);
}
