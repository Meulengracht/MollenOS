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

    // MAPPING_PERSISTENT keeps the pages allocated after these views are
    // unmapped. Return the bounce pages to the pool separately.
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
        // A forced stop gives no completed byte count, so sync caches across the
        // entire mapping, including any idle part. Drop bounce pages without
        // copying uncertain data back into the source buffer.
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
        // Leave the full range locked down and all backing pages allocated.
        // The next attempt skips completed cache work and retries only release.
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
    _In_Opt_ SHMDeviceOperation_t* operation,
    _InOut_  SHMDeviceMapping_t**  mapping)
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

    // The caller has finished all device work. Holding a walk reference blocks
    // cleanup here, and setting Cleaning blocks new list walks from starting.
    if ((operation != NULL && operation->Stopped) || current->State == SHMDeviceMappingReleasingAfterStop) {
        // Stopped is recorded at operation start separately from each mapping's
        // cleanup progress. An earlier stop pass might have skipped this mapping
        // because a reference was busy, so finish its full-range cache sync once
        // before freeing.
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
    enum SHMAccessPhase phase;
    oserr_t             status;

    if (current == NULL) {
        return OS_EOK;
    }
    context = current->Context;
    if (context->Binding != NULL) {
        // Stop cleanup may already be approved for this live mapping. Block the
        // recovery group first, then inspect only lifetime flags protected by
        // MappingLock. Never touch backing pages or device addresses that an
        // exclusive cleanup pass could be freeing.
        DMABindingRevoke(context->Binding);
        MutexLock(&context->MappingLock);
        if (current->Lifetime.Cleaning || current->State != SHMDeviceMappingStopped) {
            current->Lifetime.Abandoned = true;
            *mapping = NULL;
            MutexUnlock(&context->MappingLock);
        } else {
            MutexUnlock(&context->MappingLock);
            SHMDeviceMappingRelease(mapping);
        }
        return OS_EOK;
    }
    
    if (current->State == SHMDeviceMappingStopped) {
        SHMDeviceMappingRelease(mapping);
        return OS_EOK;
    }

    phase = SHMAccessGetPhase(&current->Access);
    if (current->Kind == SHMDeviceMappingShared || phase == SHMAccessActive ||
        phase == SHMAccessQuarantined) {
        SHMAccessQuarantine(&current->Access);
    } else if (current->State == SHMDeviceMappingReady) {
        // No background waiter can resume once the process is gone. Idle
        // unbound mappings can unmap right away, and a failed release stays listed.
        SHMAccessEnd(&current->Access);
        status = SHMDeviceUnmap(NULL, mapping);
        if (status == OS_EOK) {
            return OS_EOK;
        }
        // A list walk in progress can temporarily return OS_EBUSY. Log any
        // other errors before handing the mapping over to context cleanup.
        if (status != OS_EBUSY) {
            ERROR("SHM: releasing abandoned mapping %p failed: %u",
                current, status);
        }
    }

    // Hand the handle's original reference over to context cleanup. Since the
    // mapping is already in the list, this cannot fail or allocate.
    MutexLock(&context->MappingLock);
    current->Lifetime.Abandoned = true;
    *mapping = NULL;
    MutexUnlock(&context->MappingLock);
    return OS_EOK;
}
