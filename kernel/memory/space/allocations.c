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
#include "../shm/range.h"

static void
__MSAllocationDelete(
        _In_ struct MSAllocation* allocation)
{
    if (allocation == NULL) {
        return;
    }

    if (allocation->SHM.View != NULL) {
        ReleaseGlobalHandle(allocation->SHM.ReferenceID);
    }

    kfree(allocation->Pages.data);
    kfree(allocation);
}

oserr_t
MSAllocationCreate(
        _In_ MemorySpace_t* memorySpace,
        _In_ uuid_t         shmTag,
        _In_ vaddr_t        address,
        _In_ size_t         length,
        _In_ unsigned int   flags)
{
    struct MSContext*    context;
    struct MSAllocation* allocation;
    void*                bitmap;
    size_t               pageSize = GetMemorySpacePageSize();
    size_t               pageCount;
    oserr_t              oserr = OS_EOK;

    TRACE("MSAllocationCreate(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ", flags=0x%x)",
          memorySpace, address, length, flags);

    if (memorySpace == NULL) {
        return OS_EINVALPARAMS;
    }

    // Allocation tracking requires a memory-space context.
    context = __MSContextGet(memorySpace);
    if (context == NULL) {
        return OS_ENOTSUPPORTED;
    }

    // Address must be page aligned
    if (address & (pageSize - 1)) {
        return OS_EINVALPARAMS;
    }

    // Length will be page aligned
    pageCount = (length + (GetMemorySpacePageSize() - 1)) / GetMemorySpacePageSize();

    allocation = kmalloc(sizeof(struct MSAllocation));
    bitmap = kmalloc(BITMAP_SIZE(pageCount) * sizeof(uint32_t));
    if (allocation == NULL || bitmap == NULL) {
        kfree(allocation);
        kfree(bitmap);
        oserr = OS_EOOM;
        goto exit;
    }

    ELEMENT_INIT(&allocation->Header, 0, allocation);
    bitmap_construct(&allocation->Pages, (int)pageCount, bitmap);
    allocation->MemorySpace = memorySpace;
    allocation->SHMTag      = shmTag;
    allocation->Address     = address;
    allocation->Length      = pageCount * pageSize;
    allocation->Flags       = flags;
    allocation->References  = 1;
    allocation->CloneOf     = NULL;
    allocation->SHM = (struct MSRestrictedMapping) { 0 };
    MSContextAddAllocation(context, allocation);

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
        if (ISINRANGE(address, allocation->Address, allocation->Address + allocation->Length)) {
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
MSAllocationRelease(
        _In_ struct MSContext*    context,
        _In_ struct MSAllocation* allocation)
{
    if (allocation == NULL) {
        return OS_EINVALPARAMS;
    }

    MutexLock(&context->SyncObject);
    allocation->References--;
    if (!allocation->References) {
        list_remove(&context->Allocations, &allocation->Header);
        __MSAllocationDelete(allocation);
    }
    MutexUnlock(&context->SyncObject);
    return OS_EOK;
}

oserr_t
MSAllocationFree(
        _In_  struct MSContext*     context,
        _In_  vaddr_t               address,
        _In_  size_t                length,
        _Out_ struct MSAllocation** clonedFrom)
{
    struct MSAllocation* allocation = NULL;
    size_t               pageSize = GetMemorySpacePageSize();
    size_t               alignedLength = length;
    oserr_t              oserr;

    if (context == NULL || address == 0 || length == 0 || clonedFrom == NULL) {
        return OS_EINVALPARAMS;
    }

    // The address must be on a page boundary.
    if (address & (pageSize - 1)) {
        return OS_EINVALPARAMS;
    }

    // Make sure length is page-aligned
    if (alignedLength & (pageSize - 1)) {
        alignedLength = (alignedLength + (pageSize - 1)) & ~(pageSize - 1);
    }

    MutexLock(&context->SyncObject);
    allocation = __FindAllocation(context, address);
    if (allocation == NULL) {
        oserr = OS_ENOENT;
        goto exit;
    }

    // The allocation cannot be freed while other references exist.
    if (allocation->References > 1) {
        oserr = OS_ELINKS;
        goto exit;
    }

    // A partial free marks the range permanently unavailable.
    // The range cannot be allocated again.
    size_t block = (address - allocation->Address) / pageSize;
    size_t count = alignedLength / pageSize;
    bitmap_set(&allocation->Pages, (int)block, (int)count);

    // Return the source allocation for a clone.
    *clonedFrom = allocation->CloneOf;

    // Remove the allocation when all pages have been freed.
    if (bitmap_bits_clear_count(&allocation->Pages) == 0) {
        list_remove(&context->Allocations, &allocation->Header);
        __MSAllocationDelete(allocation);
        oserr = OS_EOK;
    } else {
        oserr = OS_EINCOMPLETE;
    }

exit:
    MutexUnlock(&context->SyncObject);
    return oserr;
}

oserr_t
MSAllocationLink(
        _In_ struct MSContext*    context,
        _In_ vaddr_t              address,
        _In_ struct MSAllocation* link)
{
    struct MSAllocation* allocation;

    if (context == NULL) {
        return OS_EINVALPARAMS;
    }

    MutexLock(&context->SyncObject);
    allocation = __FindAllocation(context, address);
    if (allocation == NULL) {
        MutexUnlock(&context->SyncObject);
        return OS_ENOENT;
    }

    allocation->CloneOf = link;
    MutexUnlock(&context->SyncObject);
    return OS_EOK;
}

oserr_t
MSAllocationFindSHM(
    _In_  struct MSContext*     context,
    _In_  vaddr_t               address,
    _In_  size_t                length,
    _In_  bool                 allowFreedPages,
    _Out_ struct MSAllocation** allocationOut)
{
    struct MSAllocation* allocation;
    struct MSAllocation* found = NULL;
    size_t               pageSize = GetMemorySpacePageSize();
    size_t               first;
    size_t               last;
    size_t               page;
    oserr_t              status = OS_EOK;

    if (length == 0 || address > UINTPTR_MAX - (length - 1)) {
        return OS_EINVALPARAMS;
    }
    if (context == NULL) {
        *allocationOut = NULL;
        return OS_EOK;
    }
    MutexLock(&context->SyncObject);
    foreach(element, &context->Allocations) {
        allocation = element->value;
        if (allocation->SHM.View == NULL) {
            continue;
        }
        if (address > allocation->Address && address - allocation->Address >= allocation->Length) {
            continue;
        }
        if (address < allocation->Address && allocation->Address - address >= length) {
            continue;
        }
        if (!__SHMRangeContains(allocation->Address, allocation->Length, address, length)) {
            status = OS_ENOTSUPPORTED;
            break;
        }
        first = (address - allocation->Address) / pageSize;
        last = (address - allocation->Address + length - 1) / pageSize;
        for (page = first; page <= last; page++) {
            if (!allowFreedPages && bitmap_bits_set(&allocation->Pages, (int)page, 1)) {
                status = OS_EINVALPARAMS;
                break;
            }
        }
        found = allocation;
        break;
    }
    MutexUnlock(&context->SyncObject);
    if (status == OS_EOK) {
        *allocationOut = found;
    }
    return status;
}
