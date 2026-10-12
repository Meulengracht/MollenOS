/**
 * Copyright 2021, Philip Meulengracht
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
 * MollenOS System Component Infrastructure
 * - The Memory component. This component has the task of managing
 *   different memory regions that map to physical components
 */

#include <assert.h>
#include <limits.h>
#include <component/memory.h>
#include <string.h>

void
SystemMemoryConstruct(
    _In_ SystemMemory_t* systemMemory,
    _In_ uintptr_t       physicalBase,
    _In_ size_t          size,
    _In_ size_t          blockSize,
    _In_ unsigned int    attributes)
{
    assert(systemMemory != NULL);
    assert(blockSize != 0);

    memset(systemMemory, 0, sizeof(*systemMemory));
    systemMemory->PhysicalBase = physicalBase;
    systemMemory->Size = size;
    systemMemory->BlockSize = blockSize;
    systemMemory->Attributes = attributes;
    // Boot setup supplies the mask regions and their free ranges separately.
}

static bool
__InDeviceRegion(
    _In_ const SystemMemory_t* systemMemory,
    _In_ paddr_t               address)
{
    const struct SystemMemoryDeviceRegion* region = &systemMemory->DeviceMemory;

    return address >= region->Base && address - region->Base < region->Length;
}

static SystemMemoryAllocatorRegion_t*
__RegionForAddress(
    _In_ SystemMemory_t* systemMemory,
    _In_ uintptr_t       address)
{
    for (int i = 0; i < systemMemory->Allocator.MaskCount; i++) {
        if (address <= systemMemory->Allocator.Masks[i]) {
            return &systemMemory->Allocator.Region[i];
        }
    }
    return NULL;
}

oserr_t
SystemMemoryAllocate(
    _In_  SystemMemory_t* systemMemory,
    _In_  size_t          memoryMask,
    _In_  int             pageCount,
    _Out_ uintptr_t*      pages)
{
    SystemMemoryAllocatorRegion_t* region;
    int                            allocated = 0;
    int                            count;
    oserr_t                        status;

    if (systemMemory == NULL || pages == NULL || pageCount <= 0) {
        return OS_EINVALPARAMS;
    }

    // Prefer high memory, preserving the lower ranges for constrained callers.
    for (int i = systemMemory->Allocator.MaskCount - 1; i >= 0; i--) {
        if (memoryMask != 0 && memoryMask < systemMemory->Allocator.Masks[i]) {
            continue;
        }
        
        region = &systemMemory->Allocator.Region[i];
        count = pageCount - allocated;
        
        SpinlockAcquireIrq(&region->Lock);
        status = MemoryStackPop(&region->Stack, &count, &pages[allocated]);
        SpinlockReleaseIrq(&region->Lock);
        if (status == OS_EOOM) {
            continue;
        }
        
        allocated += count;
        if (allocated == pageCount) {
            return OS_EOK;
        }
    }

    // If the allocation fails, cleanup and free the allocation
    if (allocated != 0) {
        status = SystemMemoryFree(systemMemory, allocated, pages);
        assert(status == OS_EOK);
        (void)status;
        memset(pages, 0, (size_t)allocated * sizeof(*pages));
    }
    return OS_EOOM;
}

oserr_t
SystemMemoryFree(
    _In_ SystemMemory_t*  systemMemory,
    _In_ int              pageCount,
    _In_ const uintptr_t* pages)
{
    SystemMemoryAllocatorRegion_t* region;
    oserr_t                        status;

    if (systemMemory == NULL || pages == NULL || pageCount <= 0) {
        return OS_EINVALPARAMS;
    }

    // Validate the whole list before returning any pages. Reserved pages must
    // only be released through their allocation record, never into a stack.
    for (int i = 0; i < pageCount; i++) {
        if (pages[i] == 0) {
            continue;
        }
        
        if (systemMemory->BlockSize == 0 || pages[i] % systemMemory->BlockSize != 0) {
            return OS_EINVALPARAMS;
        }
        
        // If the page we're trying to free is in the device region
        // or we fail to find a valid region for it, it's an invalid free.
        if (__InDeviceRegion(systemMemory, pages[i]) || 
                __RegionForAddress(systemMemory, pages[i]) == NULL) {
            return OS_EINVALPARAMS;
        }
    }

    for (int i = 0; i < pageCount; i++) {
        if (pages[i] == 0) {
            continue;
        }
        
        // Get the region
        region = __RegionForAddress(systemMemory, pages[i]);
        
        // Free page
        SpinlockAcquireIrq(&region->Lock);
        status = MemoryStackPush(&region->Stack, pages[i], 1);
        SpinlockReleaseIrq(&region->Lock);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

oserr_t
SystemMemoryInitializeDeviceRegion(
    _In_ SystemMemory_t* systemMemory,
    _In_ paddr_t         base,
    _In_ size_t          length,
    _In_ bool            cached,
    _In_ void*           storage,
    _In_ size_t          storageSize)
{
    struct SystemMemoryDeviceRegion* region;
    size_t                           pageSize;
    size_t                           pageCount;
    size_t                           required;
    oserr_t                          status;

    if (systemMemory == NULL || storage == NULL) {
        return OS_EINVALPARAMS;
    }

    region = &systemMemory->DeviceMemory;
    if (region->Length != 0) {
        return OS_EBUSY;
    }
    
    pageSize = systemMemory->BlockSize;
    if (pageSize == 0 || (pageSize & (pageSize - 1)) != 0) {
        return OS_EINVALPARAMS;
    }
    if (length == 0 || base % pageSize != 0 || length % pageSize != 0) {
        return OS_EINVALPARAMS;
    }
    if (length - 1 > (paddr_t)~(paddr_t)0 - base) {
        return OS_EINVALPARAMS;
    }

    pageCount = length / pageSize;
    required = MemoryStackCalculateSize(pageCount);
    if (pageCount > INT_MAX || required == 0) {
        return OS_ENOTSUPPORTED;
    }
    if (storageSize < required) {
        return OS_EINVALPARAMS;
    }

    status = MemoryStackConstruct(
        &region->Stack,
        pageSize,
        (uintptr_t)storage,
        storageSize
    );
    if (status != OS_EOK) {
        return status;
    }
    
    // Push initial capacity into the memory stack, and setup region
    (void)MemoryStackPush(&region->Stack, (uintptr_t)base, (int)pageCount);
    SpinlockConstruct(&region->Lock);
    region->Base = base;
    region->Length = length;
    region->Cached = cached;
    return OS_EOK;
}

oserr_t
SystemMemoryAllocateDevice(
    _In_    SystemMemory_t*                systemMemory,
    _In_    size_t                         length,
    _In_    size_t                         alignment,
    _In_    size_t                         boundary,
    _InOut_ struct SystemMemoryAllocation* allocation)
{
    struct SystemMemoryDeviceRegion* region;
    uintptr_t                        base;
    oserr_t                          status;

    if (systemMemory == NULL || allocation == NULL) {
        return OS_EINVALPARAMS;
    }
    if (allocation->Owner != NULL) {
        return OS_EBUSY;
    }

    region = &systemMemory->DeviceMemory;
    if (region->Length == 0) {
        return OS_ENOTSUPPORTED;
    }
    
    SpinlockAcquireIrq(&region->Lock);
    status = MemoryStackPopContiguous(
        &region->Stack,
        length,
        alignment,
        boundary,
        &base
    );
    SpinlockReleaseIrq(&region->Lock);
    if (status != OS_EOK) {
        return status;
    }
    
    allocation->Owner = systemMemory;
    allocation->PhysicalBase = base;
    allocation->Length = DIVUP(length, systemMemory->BlockSize) * systemMemory->BlockSize;
    allocation->Cached = systemMemory->DeviceMemory.Cached;
    return OS_EOK;
}

oserr_t
SystemMemoryReleaseAllocation(
    _InOut_ struct SystemMemoryAllocation* allocation)
{
    struct SystemMemoryDeviceRegion* region;
    size_t                           pageSize;
    size_t                           offset;
    oserr_t                          status;

    if (allocation == NULL) {
        return OS_EINVALPARAMS;
    }
    if (allocation->Owner == NULL) {
        return OS_EOK;
    }

    region = &allocation->Owner->DeviceMemory;
    pageSize = allocation->Owner->BlockSize;
    if (allocation->PhysicalBase < region->Base || allocation->Length == 0) {
        return OS_EINVALPARAMS;
    }

    offset = (size_t)(allocation->PhysicalBase - region->Base);
    if (offset > region->Length || allocation->Length > region->Length - offset) {
        return OS_EINVALPARAMS;
    }
    if (offset % pageSize != 0 || allocation->Length % pageSize != 0) {
        return OS_EINVALPARAMS;
    }

    SpinlockAcquireIrq(&region->Lock);
    status = MemoryStackPush(
        &region->Stack,
        (uintptr_t)allocation->PhysicalBase,
        (int)(allocation->Length / pageSize)
    );
    SpinlockReleaseIrq(&region->Lock);
    if (status == OS_EOK) {
        memset(allocation, 0, sizeof(*allocation));
    }
    return status;
}

oserr_t
SystemMemoryContainsAddress(
    _In_ SystemMemory_t* systemMemory,
    _In_ uintptr_t       address)
{
    if (address >= systemMemory->PhysicalBase &&
        address - systemMemory->PhysicalBase < systemMemory->Size) {
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}
