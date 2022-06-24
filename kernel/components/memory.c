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
#include <component/memory.h>

void
SystemMemoryConstruct(
        _In_ SystemMemory_t* systemMemory,
        _In_ uintptr_t       physicalBase,
        _In_ size_t          size,
        _In_ size_t          blockSize,
        _In_ unsigned int    attributes)
{
    assert(systemMemory != NULL);

    systemMemory->PhysicalBase = physicalBase;
    systemMemory->Size = size;
    systemMemory->BlockSize = blockSize;
    systemMemory->Attributes = attributes;

    // initialize the allocator if the memory region has data
    if (size != 0) {
        assert(blockSize != 0);

        // retrieve memory masks for this region which should be specified by
        // the underlying architecture

        // do not add any memory pages yet, this will be done when the core memory
        // map gets transferred to regions
    }
}

static SystemMemoryAllocatorRegion_t*
__GetAppropriateAllocator(
        _In_  SystemMemory_t* systemMemory,
        _In_  size_t          memoryMask)
{
    int i;
    if (memoryMask == 0) {
        // if caller do not care then return highest allocator
        return &systemMemory->Allocator.Region[systemMemory->Allocator.MaskCount - 1];
    }

    for (i = systemMemory->Allocator.MaskCount - 1; i > 0; i--) {
        if (memoryMask >= systemMemory->Allocator.Masks[i]) {
            break;
        }
    }
    return &systemMemory->Allocator.Region[i];
}

OsStatus_t
SystemMemoryAllocate(
        _In_ SystemMemory_t* systemMemory,
        _In_ size_t          memoryMask,
        _In_ int             pageCount,
        _In_ uintptr_t*      pages)
{
    OsStatus_t osStatus;
    int        pagesAllocated = pageCount;
    SystemMemoryAllocatorRegion_t* region;

    // default to highest allocator
    region = &systemMemory->Allocator.Region[systemMemory->Allocator.MaskCount - 1];
    if (memoryMask) {
        for (int i = systemMemory->Allocator.MaskCount - 1; i >= 0; i--) {
            if (memoryMask >= systemMemory->Allocator.Masks[i]) {
                region = &systemMemory->Allocator.Region[i];
            }
        }
    }

    IrqSpinlockAcquire(&region->Lock);
    osStatus = MemoryStackPop(&region->Stack, &pagesAllocated, pages);
    if (osStatus == OsIncomplete) {
        MemoryStackPushMultiple(&region->Stack, pages, pagesAllocated);
        osStatus = OsOutOfMemory;
    }
    IrqSpinlockRelease(&region->Lock);
    return osStatus;
}

OsStatus_t
SystemMemoryFree(
        _In_ SystemMemory_t* systemMemory,
        _In_ int             pageCount,
        _In_ uintptr_t*      pages)
{

    return OsOK;
}

OsStatus_t
SystemMemoryContainsAddress(
        _In_ SystemMemory_t* systemMemory,
        _In_ uintptr_t       address)
{
    if (address >= systemMemory->PhysicalBase &&
        address < (systemMemory->PhysicalBase + systemMemory->Size)) {
        return OsOK;
    }
    return OsError;
}
