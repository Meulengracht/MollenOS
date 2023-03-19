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

static void
__MSAllocationDelete(
        _In_ struct MSAllocation* allocation)
{
    if (allocation == NULL) {
        return;
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

    // We only support allocation tracking for spaces with context
    if (memorySpace->Context == NULL) {
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

    // Address we are freeing *must* be on a page boundary
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

    // Disallow frees on cloned allocations
    if (allocation->References > 1) {
        oserr = OS_ELINKS;
        goto exit;
    }

    // We support partial freeing, by marking a range of the memory
    // permanently unavailable. It's important to note that we currently
    // don't support re-allocation of that memory.
    size_t block = (address - allocation->Address) / pageSize;
    size_t count = alignedLength / pageSize;
    bitmap_set(&allocation->Pages, (int)block, (int)count);

    // Store the cloned from
    *clonedFrom = allocation->CloneOf;

    // Are we now fully freed?
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
