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
#include <ds/list.h>
#include <heap.h>
#include <mutex.h>
#include "private.h"

#define __SZ_TO_PGSZ(_sz, _out) { \
    size_t _pgsz = GetMemorySpacePageSize(); \
    _out = DIVUP(_sz, _pgsz) * _pgsz; \
}

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

    if (memorySpace == NULL) {
        return OS_EINVALPARAMS;
    }

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
    allocation->SHMTag      = UUID_INVALID;
    allocation->Address     = address;
    allocation->Length      = length;
    allocation->Flags       = flags;
    allocation->References  = 1;
    allocation->CloneOf     = NULL;
    MSContextAddAllocation(memorySpace->Context, allocation);

exit:
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
MSAllocationFree(
        _In_  struct MSContext*     context,
        _In_  vaddr_t*              address,
        _In_  size_t*               size,
        _Out_ struct MSAllocation** clonedFrom)
{
    struct MSAllocation* allocation = NULL;
    oserr_t              oserr;

    if (context == NULL) {
        return OS_EINVALPARAMS;
    }

    MutexLock(&context->SyncObject);
    allocation = __FindAllocation(context, *address);
    if (allocation == NULL) {
        oserr = OS_ENOENT;
        goto exit;
    }

    // We support reductions, but only shrinking. Callers can only expand and
    // shrink it at the end of the allocation. It does not matter what the actual
    // address is, as long as the address is inside the bounds of the allocation.
    if (*size < allocation->Length) {
        // Let's reduce the mapping, but *only* as long as the allocation is not
        // cloned.
        if (allocation->References > 1) {
            oserr = OS_EPERMISSIONS;
            goto exit;
        }
    }

    // If other callers are using the same memory, for instance when cloned,
    // then we must not neccesarily free the actual allocation.
    allocation->References--;
    if (allocation->References) {
        oserr = OS_EINCOMPLETE;
        goto exit;
    }

    // store the start of the actual allocation address so the caller
    // knows where to start freeing from.
    *address = allocation->Address;

    if (*size < allocation->Length) {
        size_t alignedLength;
        __SZ_TO_PGSZ(allocation->Length - *size, alignedLength)

        // partial free, but always align to page size
        allocation->Length = alignedLength;
        *size = alignedLength;
        *clonedFrom = NULL;
    } else {
        // full free
        *size = 0;
        *clonedFrom = allocation->CloneOf;
        list_remove(&context->Allocations, &allocation->Header);
        kfree(allocation);
    }
    oserr = OS_EOK;

exit:
    MutexUnlock(&context->SyncObject);
    return oserr;
}

void
MSAllocationLink(
        _In_ struct MSContext*    context,
        _In_ vaddr_t              address,
        _In_ struct MSAllocation* link)
{
    struct MSAllocation* allocation;
    MutexLock(&context->SyncObject);
    allocation = __FindAllocation(context, address);
    if (allocation) {
        allocation->CloneOf = link;
    }
    MutexUnlock(&context->SyncObject);
}
