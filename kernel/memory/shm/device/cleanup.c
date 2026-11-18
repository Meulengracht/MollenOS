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

// Remove a kernel view and clear its address, but only when the unmap works.
// The views use MAPPING_PERSISTENT, so unmapping them never frees the pages
// behind them. Pool units are given back separately.
static oserr_t
__UnmapKernelView(
    _InOut_ vaddr_t*    view,
    _In_    size_t      length,
    _In_    const char* name)
{
    oserr_t status;

    if (*view == 0) {
        return OS_EOK;
    }

    status = MemorySpaceUnmap(GetDomainMemorySpace(), *view, length);
    if (status != OS_EOK) {
        ERROR("SHMDeviceReleaseBounce: MemorySpaceUnmap %s view %p, length %zu failed: %u",
              name, (void*)*view, length, status);
        return status;
    }
    *view = 0;
    return OS_EOK;
}

// Give every unit that is still allocated back to the pool. A unit that fails
// keeps its record, and the loop goes on with the next one, so one bad unit
// does not hold back the others. A released record is zeroed by the pool, so a
// retry skips it and never returns the same pages twice. Only the units that
// went back stop counting as held translated bounce storage.
static oserr_t
__ReturnBounceUnits(
    _In_    SHMDeviceContext_t* context,
    _InOut_ SHMDeviceBounce_t*  bounce)
{
    size_t  returned = 0;
    oserr_t firstError = OS_EOK;
    oserr_t status;

    for (size_t i = 0; i < bounce->UnitCount; i++) {
        struct SystemMemoryAllocation* unit = &bounce->Units[i];
        paddr_t                        base = unit->PhysicalBase;
        size_t                         length = unit->Length;

        if (unit->Owner == NULL) {
            continue;
        }

        status = SystemMemoryReleaseAllocation(unit);
        if (status != OS_EOK) {
            ERROR("SHMDeviceReleaseBounce: SystemMemoryReleaseAllocation unit %zu of %zu (base %llx, length %zu) failed: %u",
                  i, bounce->UnitCount, (unsigned long long)base, length, status);
            if (firstError == OS_EOK) {
                firstError = status;
            }
            continue;
        }
        returned += length;
    }

    if (bounce->Translated) {
        SHMDeviceContextRemoveBounceStorage(context, returned);
    }
    return firstError;
}

// Free the pool side of the bounce storage: the bounce view, then the units,
// then the unit record array. Each step runs only when the one before it
// succeeded, and the first failure is returned.
static oserr_t
__ReleaseBounceStorage(
    _In_    SHMDeviceContext_t* context,
    _InOut_ SHMDeviceBounce_t*  bounce)
{
    oserr_t status;

    // The bounce view must be gone before any unit goes back to the pool. If
    // it stays mapped, the units stay allocated as well.
    status = __UnmapKernelView(
        &bounce->BounceMapping,
        bounce->BounceMappingLength,
        "bounce"
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __ReturnBounceUnits(context, bounce);
    if (status != OS_EOK) {
        return status;
    }

    // The record array is the only way to find units that are still
    // allocated, so it is freed only now that all of them are back.
    if (bounce->Units != NULL) {
        kfree(bounce->Units);
        bounce->Units = NULL;
        bounce->UnitCount = 0;
    }
    return OS_EOK;
}

oserr_t
SHMDeviceReleaseBounce(
    _In_    SHMDeviceContext_t* context,
    _InOut_ SHMDeviceBounce_t*  bounce)
{
    oserr_t status;

    // The source view maps the client's pages, not pool pages, so it is
    // removed whatever happened to the bounce storage. When both fail, the
    // storage error is returned because it came first. The source view
    // failure is still logged by __UnmapKernelView.
    status = __ReleaseBounceStorage(context, bounce);
    if (status != OS_EOK) {
        (void)__UnmapKernelView(
            &bounce->SourceMapping,
            bounce->SourceMappingLength,
            "source"
        );
        return status;
    }

    return __UnmapKernelView(
        &bounce->SourceMapping,
        bounce->SourceMappingLength,
        "source"
    );
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

// Stop the device from reaching the mapping: sync caches after a confirmed
// stop, leave the Ready state and remove the device addresses. The caller
// frees the storage only after this succeeds.
static oserr_t
__RemoveDeviceAccess(
    _In_ SHMDeviceMapping_t* mapping,
    _In_ bool                stopped)
{
    oserr_t status;

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
    return OS_EOK;
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

    // Device access is removed first, and nothing below runs until it is.
    // AddressMapping is cleared only when that removal succeeds, so a NULL
    // pointer here means an earlier attempt got past it and then failed while
    // freeing the storage. Any cache work the release needed ran before the
    // addresses were removed, and the storage may now be partly back in the
    // pool. Syncing the caches again could discard cache lines that belong to
    // the pool's next user, so resume at the steps below instead.
    if (mapping->AddressMapping != NULL) {
        status = __RemoveDeviceAccess(mapping, stopped);
        if (status != OS_EOK) {
            return status;
        }
    }

    // The steps from here on can run again on a retry. Lifting the buffer
    // lockdown does nothing for a range that is already removed, and clearing
    // region faults only touches regions that are still faulted.
    if (stopped) {
        // Device addresses are now gone, so lift the fault or range lock so
        // healthy regions or the buffer can be used again. This is done
        // before the bounce storage is released, so the client gets its
        // buffer back even when that release fails and has to be retried.
        if (mapping->Regions != NULL) {
            status = SHMDeviceRegionsClearFaults(mapping);
            if (status != OS_EOK) {
                return status;
            }
        } else {
            SHMAccessCompleteStop(&mapping->Access);
        }
    }

    // A failed release keeps the mapping in its releasing state, with its
    // buffer reference and extents, so a later unmap or stop pass resumes
    // here. The release itself repeats only the parts that failed.
    if (__SHMDeviceMappingIsStreaming(mapping)) {
        status = SHMDeviceReleaseBounce(mapping->Context, &mapping->Stream.Bounce);
        if (status != OS_EOK) {
            ERROR("SHMDeviceMappingCleanup: SHMDeviceReleaseBounce mapping %p failed: %u", mapping, status);
            return status;
        }
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
