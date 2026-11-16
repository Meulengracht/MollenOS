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
 */

#include <assert.h>
#include <debug.h>
#include <ddk/barrier.h>
#include <limits.h>
#include <handles/global.h>
#include <heap.h>
#include <memoryspace.h>

#include "private.h"
#include "device_private.h"
#include "../utils.h"

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
        // finished. So sync the caches for the whole mapping, even the parts
        // that were not in use. Bounce pages are dropped without copying their
        // data back to the source buffer, because that data may be incomplete.
        SHMDeviceCompleteForCpu(mapping, 0, mapping->Length);
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
        SHMAccessCompleteStop(&mapping->Access);
    }

    if (__SHMDeviceMappingIsStreaming(mapping)) {
        SHMDeviceReleaseBounce(&mapping->Stream.Bounce);
    }

    __ReleaseMemoryHandleReference(mapping->Access.ReferenceID, __func__);
    mapping->Access.ReferenceID = UUID_INVALID;
    mapping->Access.Buffer = NULL;
    kfree(mapping->Extents);
    mapping->Extents = NULL;
    mapping->ExtentCount = 0;
    mapping->State = SHMDeviceMappingStopped;
    return OS_EOK;
}

oserr_t
SHMDeviceUnmap(
    _In_    SHMDeviceOperation_t* operation,
    _InOut_ SHMDeviceMapping_t**  mapping)
{
    SHMDeviceMapping_t* current;
    SHMDeviceContext_t* context;
    oserr_t             status;

    if (mapping == NULL || *mapping == NULL) {
        return OS_EOK;
    }

    current = *mapping;
    context = current->Context;

    if (!SHMDeviceOperationIsValid(operation, context, current, SHMDeviceOperationRelease) &&
        !SHMDeviceOperationIsValid(operation, context, NULL, SHMDeviceOperationSubmit)) {
        return OS_EINVALPARAMS;
    }

    MutexLock(&context->MappingLock);
    if (current->Lifetime.Cleaning) {
        MutexUnlock(&context->MappingLock);
        return OS_EBUSY;
    }

    if (current->State == SHMDeviceMappingStopped) {
        MutexUnlock(&context->MappingLock);
        SHMDeviceMappingRelease(mapping);
        return OS_EOK;
    }

    if (atomic_load(&current->Lifetime.References) !=
        (current->Operation.Setup >= SHMDeviceOperationSetupMapping ? 2U : 1U)) {
        MutexUnlock(&context->MappingLock);
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
    } else {
        // Already fully cleaned, so only the handle's reference is left.
        MutexUnlock(&context->MappingLock);
        SHMDeviceMappingRelease(mapping);
    }
    return OS_EOK;
}
