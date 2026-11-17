/**
 * Copyright, Philip Meulengracht
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
 * Cleanup and unmapping for SHM device mappings. Handles linking mappings onto
 * a context, freeing bounce pages and device addresses, and releasing
 * abandoned mappings after hardware stop is confirmed.
 */

#include <assert.h>
#include <debug.h>
#include <ddk/barrier.h>
#include <limits.h>
#include <handles/global.h>
#include <heap.h>
#include <memoryspace.h>

#include "private.h"

void
SHMDeviceReleaseBounce(
    _InOut_ SHMDeviceBounce_t* bounce)
{
    oserr_t status;

    // The views use MAPPING_PERSISTENT, so unmapping them does not free the
    // pages. The bounce pages are given back to the device pool separately.
    if (bounce->BounceMapping != 0) {
        status = MemorySpaceUnmap(
            GetDomainMemorySpace(),
            bounce->BounceMapping,
            bounce->Allocation.Length
        );
        if (status != OS_EOK) {
            ERROR("SHMDeviceReleaseBounce: MemorySpaceUnmap bounce %p failed: %u",
                  (void*)bounce->BounceMapping, status);
        }
        bounce->BounceMapping = 0;
    }

    status = SystemMemoryReleaseAllocation(&bounce->Allocation);
    if (status != OS_EOK) {
        ERROR("SHMDeviceReleaseBounce: SystemMemoryReleaseAllocation failed: %u", status);
    }
    if (bounce->SourceMapping != 0) {
        status = MemorySpaceUnmap(
            GetDomainMemorySpace(),
            bounce->SourceMapping,
            bounce->SourceMappingLength
        );
        if (status != OS_EOK) {
            ERROR("SHMDeviceReleaseBounce: MemorySpaceUnmap source %p failed: %u",
                  (void*)bounce->SourceMapping, status);
        }
        bounce->SourceMapping = 0;
    }
}

void
SHMDeviceMappingLink(
    _In_ SHMDeviceMapping_t* mapping)
{
    SHMDeviceContext_t* context = mapping->Context;

    MutexLock(&context->MappingLock);
    mapping->Lifetime.Next = context->Mappings;
    if (context->Mappings != NULL) {
        context->Mappings->Lifetime.Previous = mapping;
    }
    context->Mappings = mapping;
    MutexUnlock(&context->MappingLock);
}

oserr_t
SHMDeviceMappingAcquire(
    _In_ SHMDeviceMapping_t* mapping)
{
    unsigned int references;

    references = atomic_load(&mapping->Lifetime.References);
    do {
        if (references == 0) {
            return OS_ECANCELLED;
        }

        if (references == UINT_MAX) {
            return OS_EOVERFLOW;
        }
    } while (!atomic_compare_exchange_weak(
        &mapping->Lifetime.References, &references, references + 1
    ));
    return OS_EOK;
}

void
SHMDeviceMappingRelease(
    _InOut_ SHMDeviceMapping_t** mapping)
{
    SHMDeviceMapping_t* released;
    SHMDeviceContext_t* context;

    if (*mapping == NULL) {
        return;
    }

    released = *mapping;
    *mapping = NULL;
    
    if (atomic_fetch_sub(&released->Lifetime.References, 1) != 1) {
        return;
    }

    // An internal reference keeps the struct in memory even when a live handle's
    // device addresses and backing pages were already freed after a hardware stop.
    assert(released->State == SHMDeviceMappingStopped);
    context = released->Context;
    MutexLock(&context->MappingLock);
    if (released->Lifetime.Previous != NULL) {
        released->Lifetime.Previous->Lifetime.Next = released->Lifetime.Next;
    } else {
        context->Mappings = released->Lifetime.Next;
    }
    if (released->Lifetime.Next != NULL) {
        released->Lifetime.Next->Lifetime.Previous = released->Lifetime.Previous;
    }
    MutexUnlock(&context->MappingLock);

    // Every open region session holds a mapping reference, so by the time the
    // last reference drops there can be no sessions left.
    if (released->Regions != NULL) {
        assert(released->Regions->SessionCount == 0);
        kfree(released->Regions);
    }

    kfree(released);
    SHMDeviceContextRelease(&context);
}

oserr_t
SHMDeviceMappingCleanup(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ bool                stopped)
{
    oserr_t status;

    if (mapping->State == SHMDeviceMappingStopped) {
        return OS_EOK;
    }
    assert(mapping->Lifetime.Cleaning);

    if (stopped && mapping->State != SHMDeviceMappingReleasingAfterStop) {
        // After a forced stop there is no count of how many bytes the device
        // finished. For region mappings, move active tokens to stopped, sync
        // caches once live users drain, and retire them. For other mappings,
        // sync caches across the whole range and drop bounce pages without
        // copying uncertain bytes back to the source buffer.
        if (mapping->Regions != NULL) {
            status = SHMDeviceRegionsStop(mapping);
            if (status != OS_EOK) {
                return status;
            }
        } else {
            SHMDeviceCompleteForCpu(mapping, 0, mapping->Length);
        }

        if (mapping->Kind == SHMDeviceMappingPersistent) {
            mapping->Persistent.Uses = 0;
        }
        
        mapping->State = SHMDeviceMappingReleasingAfterStop;
    }

    if (mapping->State == SHMDeviceMappingReady) {
        mapping->State = SHMDeviceMappingReleasing;
    }
    
    status = DMAAddressMappingRelease(&mapping->AddressMapping);
    if (status != OS_EOK) {
        // Keep the whole buffer range locked and all backing pages allocated.
        // A later attempt skips the cache work that is done and retries only
        // the release.
        return status;
    }

    if (stopped) {
        // Device addresses are now gone, so lift the fault or range lock so
        // healthy regions or the buffer can be used again.
        if (mapping->Regions != NULL) {
            status = SHMDeviceRegionsClearFaults(mapping);
            if (status != OS_EOK) {
                return status;
            }
        } else {
            SHMAccessCompleteStop(&mapping->Access);
        }
    }

    if (__SHMDeviceMappingIsStreaming(mapping)) {
        SHMDeviceReleaseBounce(&mapping->Stream.Bounce);
    }

    // Clear Usage pointers before dropping the buffer reference that keeps them alive.
    if (mapping->Regions != NULL) {
        for (unsigned int i = 0; i < mapping->Regions->Count; i++) {
            mapping->Regions->Entries[i].Binding.Usage = NULL;
        }
    }
    __ReleaseMemoryHandleReference(mapping->Access.ReferenceID, __func__);
    mapping->Access.ReferenceID = UUID_INVALID;
    mapping->Access.Buffer = NULL;
    kfree(mapping->Extents);
    mapping->Extents = NULL;
    mapping->ExtentCount = 0;
    mapping->State = SHMDeviceMappingStopped;
    TRACE("SHMDeviceMappingCleanup: mapping %p device resources freed, stopped %i", mapping, (int)stopped);
    return OS_EOK;
}

oserr_t
SHMDeviceUnmap(
    _In_    SHMDeviceOperation_t* operation,
    _InOut_ SHMDeviceMapping_t**  mapping)
{
    SHMDeviceMapping_t* current;
    SHMDeviceContext_t* context;
    unsigned int        references;
    unsigned int        expectedReferences;
    oserr_t             status;

    if (mapping == NULL || *mapping == NULL) {
        return OS_EOK;
    }

    current = *mapping;
    context = current->Context;

    if (!SHMDeviceOperationIsValid(operation, context, current, SHMDeviceOperationRelease) &&
        !SHMDeviceOperationIsValid(operation, context, NULL, SHMDeviceOperationSubmit)) {
        WARNING("SHMDeviceUnmap: mapping %p operation %p is not a valid release or submit token",
                current, operation);
        return OS_EINVALPARAMS;
    }

    MutexLock(&context->MappingLock);
    // All region sessions on this mapping must close before unmapping.
    if (current->Lifetime.Cleaning ||
        (current->Regions != NULL && current->Regions->SessionCount != 0)) {
        MutexUnlock(&context->MappingLock);
        return OS_EBUSY;
    }

    if (current->State == SHMDeviceMappingStopped) {
        MutexUnlock(&context->MappingLock);
        SHMDeviceMappingRelease(mapping);
        return OS_EOK;
    }

    references = atomic_load(&current->Lifetime.References);
    expectedReferences = current->Operation.Setup >= SHMDeviceOperationSetupMapping ? 2U : 1U;
    if (references != expectedReferences) {
        MutexUnlock(&context->MappingLock);
        WARNING("SHMDeviceUnmap: mapping %p has %u references, expected %u",
                current, references, expectedReferences);
        return OS_EBUSY;
    }
    current->Lifetime.Cleaning = true;
    MutexUnlock(&context->MappingLock);

    // The caller has finished all device work. The reference count check above
    // showed that no list walk holds this mapping, and Cleaning now keeps new
    // walks and other cleanups away.
    if (operation->Stopped || current->State == SHMDeviceMappingReleasingAfterStop) {
        // The Stopped flag is saved when the operation starts, and it is
        // separate from how far each mapping's cleanup got. An earlier stop
        // pass may have skipped this mapping because it was busy, so do the
        // full range cache sync here before freeing.
        status = SHMDeviceMappingCleanup(current, true);
    } else if (SHMAccessGetPhase(&current->Access) != SHMAccessIdle) {
        status = OS_EBUSY;
    } else {
        status = SHMDeviceMappingCleanup(current, false);
    }

    MutexLock(&context->MappingLock);
    current->Lifetime.Cleaning = false;
    MutexUnlock(&context->MappingLock);
    if (status == OS_EOK) {
        SHMDeviceMappingRelease(mapping);
    }
    return status;
}

oserr_t
SHMDeviceAbandon(
    _InOut_ SHMDeviceMapping_t** mapping)
{
    SHMDeviceMapping_t* current = *mapping;
    SHMDeviceContext_t* context;

    if (current == NULL) {
        return OS_EOK;
    }
    context = current->Context;

    // Block the group from starting new DMA work before looking at the
    // mapping's state. A stop cleanup pass may already be freeing its
    // addresses and pages, so only read state that MappingLock protects.
    DMABindingRevoke(context->Binding);
    MutexLock(&context->MappingLock);
    if (current->Lifetime.Cleaning || current->State != SHMDeviceMappingStopped) {
        // Hand the handle's reference to the context list. The mapping stays
        // there until the group confirms the hardware stopped, even if it is
        // idle, because the device could still be using it.
        current->Lifetime.Abandoned = true;
        *mapping = NULL;
        MutexUnlock(&context->MappingLock);
        TRACE("SHMDeviceAbandon: mapping %p handed to context %p", current, context);
    } else {
        // Already fully cleaned, so only the handle's reference is left.
        MutexUnlock(&context->MappingLock);
        SHMDeviceMappingRelease(mapping);
    }
    return OS_EOK;
}
