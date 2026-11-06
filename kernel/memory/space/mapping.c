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

#include "private.h"
#include "../shm/private.h"

vaddr_t
MSMappingAllocate(
    _In_ MemorySpace_t*                memorySpace,
    _In_ struct MemorySpaceMapOptions* options,
    _In_ size_t                        length)
{
    struct MSContext*    context = __MSContextGet(memorySpace);
    struct MSAllocation* allocation;
    vaddr_t              address;
    oserr_t              oserr;

    if (context == NULL) {
        ERROR("MSMappingAllocate: requested process memory for non-process memory space");
        return 0;
    }

    if (options->SHMView != NULL) {
        oserr = AcquireGlobalHandle(options->SHMViewID, HandleTypeSHMView, NULL);
        if (oserr != OS_EOK) {
            return 0;
        }
    }
    address = DynamicMemoryPoolAllocate(&context->Heap, length);
    if (address == 0) {
        ERROR("MSMappingAllocate: cannot allocate 0x%zx bytes of memory", length);
    } else {
        // Tracking kernel allocations would recurse through kmalloc here.
        oserr = MSAllocationCreate(memorySpace, options->SHMTag, address, length, options->Flags);
        if (oserr != OS_EOK) {
            ERROR("MSMappingAllocate: cannot register allocation");
            DynamicMemoryPoolFree(&context->Heap, address);
            address = 0;
        }
    }
    if (options->SHMView != NULL) {
        if (address == 0) {
            ReleaseGlobalHandle(options->SHMViewID);
        } else {
            allocation = MSAllocationLookup(context, address);
            allocation->SHM.ReferenceID = options->SHMViewID;
            allocation->SHM.View = options->SHMView;
            allocation->SHM.Offset = options->SHMOffset;
        }
    }
    return address;
}

oserr_t
MSMappingRollback(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         length)
{
    struct MSContext*    context = __MSContextGet(memorySpace);
    struct MSAllocation* allocation;

    // The caller must hold UserCopyLock from mapping creation through rollback.
    // The allocation stays valid. No new allocation is needed on the error path.
    allocation = MSAllocationLookup(context, address);
    allocation->SHM.CleanupPending = true;
    return MSUnmap(memorySpace, address, length);
}

static struct MSAllocation*
__FindPendingCleanup(
    _In_ struct MSContext* context,
    _In_ uuid_t            viewID)
{
    struct MSAllocation* allocation;
    struct MSAllocation* result = NULL;

    // The caller must hold UserCopyLock until it finishes using the result.
    MutexLock(&context->SyncObject);
    foreach(element, &context->Allocations) {
        allocation = element->value;
        if (allocation->SHM.ReferenceID == viewID && allocation->SHM.CleanupPending) {
            result = allocation;
            break;
        }
    }
    MutexUnlock(&context->SyncObject);
    return result;
}

oserr_t
MSMappingCleanup(
    _In_ MemorySpace_t* memorySpace,
    _In_ uuid_t         viewID)
{
    struct MSContext*    context = __MSContextGet(memorySpace);
    struct MSAllocation* allocation;
    oserr_t              status = OS_EOK;

    while ((allocation = __FindPendingCleanup(context, viewID)) != NULL) {
        status = MSUnmap(memorySpace, allocation->Address, allocation->Length);
        if (status != OS_EOK) {
            break;
        }
    }
    return status;
}

oserr_t
MSMappingCloneOptions(
    _In_  struct MSAllocation*          allocation,
    _In_  vaddr_t                       address,
    _In_  size_t                        length,
    _In_  unsigned int                  memoryFlags,
    _In_  unsigned int                  placementFlags,
    _Out_ struct MemorySpaceMapOptions* options)
{
    size_t pageSize     = GetMemorySpacePageSize();
    size_t pageOffset = address % pageSize;

    if ((placementFlags & MAPPING_VIRTUAL_MASK) != MAPPING_VIRTUAL_PROCESS) {
        return OS_ENOTSUPPORTED;
    }

    if (length > SIZE_MAX - pageOffset - (pageSize - 1)) {
        return OS_EINVALPARAMS;
    }

    *options = (struct MemorySpaceMapOptions) {
        .SHMTag = allocation->SHM.View->Buffer->ID,
        .SHMView = allocation->SHM.View,
        .SHMViewID = allocation->SHM.ReferenceID,
        .SHMOffset = allocation->SHM.Offset + address - pageOffset - allocation->Address,
        .Length = DIVUP(length + pageOffset, pageSize) * pageSize,
        .Mask = __MASK,
        .Flags = MAPPING_USERSPACE | MAPPING_PERSISTENT | MAPPING_COMMIT |
                 (memoryFlags & (MAPPING_READONLY | MAPPING_EXECUTABLE)) |
                 (allocation->Flags & MAPPING_NOCACHE),
        .PlacementFlags = MAPPING_VIRTUAL_PROCESS | MAPPING_PHYSICAL_FIXED
    };

    return SHMViewValidateMapping(
        options->SHMView,
        options->SHMOffset,
        options->Length,
        options->Flags
    );
}
