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
 * Translated address spaces. The device does not use physical addresses here.
 * It uses IOVAs (I/O virtual addresses), which the IOMMU translates to the
 * physical address of the buffer. This file creates and destroys such spaces
 * and prepares, installs and removes the mappings inside them. The IOMMU
 * hardware itself is driven by the backend callbacks of the unit.
 */

#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <limits.h>
#include <string.h>
#include "private.h"

/**
 * @brief Software record of one mapping in a translated address space.
 *
 * It ties together the IOVA range the device uses, the backend's own record
 * of the mapping, and a private copy of the physical extents behind it.
 *
 * Config points into the Extents copy at the end of this allocation. The
 * backend may keep those pointers, even after a call that timed out, so this
 * allocation is freed last. The IOVA range stays reserved until the backend
 * reports that the removal has finished, because the device can reach the
 * memory until then. Shared table pages belong to the backend space and are
 * freed only when that space is destroyed.
 */
struct DMATranslatedMapping {
    DMAIOVAAllocation_t*    Allocation;
    void*                   BackendMapping;
    DMAIOMMUMappingConfig_t Config;
    SHMSG_t                 Extents[];
};

oserr_t
DMATranslatedSpaceDestroy(
    _In_ struct DMAAddressSpace* space)
{
    DMAIOMMUUnit_t* unit = space->Translated.Unit;
    oserr_t         status;

    // Every mapping is gone at this point. If the allocator says some IOVA
    // ranges are still in use, leave the backend root and the unit's space
    // count alone. Freeing memory that something may still use is worse.
    status = DMAIOVAAllocatorDestroy(&space->Translated.Allocator);
    if (status != OS_EOK) {
        ERROR("DMATranslatedSpaceDestroy: translated space %p kept, IOVA allocator destroy failed: %u",
              (void*)space, status);
        return status;
    }

    MutexLock(&unit->Lock);
    if (space->Translated.BackendSpace != NULL) {
        unit->Ops.DestroySpace(unit->Context, space->Translated.BackendSpace);
    }
    assert(unit->Spaces != 0);
    unit->Spaces--;
    MutexUnlock(&unit->Lock);

    TRACE("DMATranslatedSpaceDestroy: translated space %p destroyed on unit %p", (void*)space, (void*)unit);
    return OS_EOK;
}

// Add a new space to the unit's space count. Fails if the unit has already
// failed, or if the count would overflow.
static oserr_t
__CountSpaceInUnit(
    _In_    DMAIOMMUUnit_t*         unit,
    _InOut_ struct DMAAddressSpace* space)
{
    oserr_t status;

    MutexLock(&unit->Lock);
    status = DMAAddressSpaceCheck(space);
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        WARNING("__CountSpaceInUnit: unit %p has failed, a new space cannot be counted: %u", (void*)unit, status);
        return status;
    }

    if (unit->Spaces == UINT_MAX) {
        MutexUnlock(&unit->Lock);
        ERROR("__CountSpaceInUnit: unit %p cannot count any more address spaces", (void*)unit);
        return OS_EOVERFLOW;
    }

    unit->Spaces++;
    MutexUnlock(&unit->Lock);
    return OS_EOK;
}

// Ask the backend for the translation root of a new space. The unit health is
// checked before the call and once more after it, all while holding Lock. The
// space is created if the unit is healthy at the end. A failure after this
// point is caught by the health checks done on every later call for this space.
static oserr_t
__CreateBackendSpace(
    _InOut_ struct DMAAddressSpace* space)
{
    DMAIOMMUUnit_t* unit = space->Translated.Unit;
    oserr_t         status;

    MutexLock(&unit->Lock);
    status = DMAAddressSpaceCheck(space);
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        WARNING("__CreateBackendSpace: unit %p has failed, a new space cannot be created: %u", (void*)unit, status);
        return status;
    }

    status = unit->Ops.CreateSpace(
        unit->Context,
        &space->Translated.BackendSpace
    );
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        ERROR("__CreateBackendSpace: backend CreateSpace on unit %p failed: %u", (void*)unit, status);
        return status;
    }

    status = DMAAddressSpaceCheck(space);
    MutexUnlock(&unit->Lock);
    if (status != OS_EOK) {
        WARNING("__CreateBackendSpace: unit %p failed while creating a space: %u", (void*)unit, status);
    }
    return status;
}

oserr_t
DMAAddressSpaceCreateTranslated(
    _In_    DMAIOMMUUnit_t*          unit,
    _In_    const DMAIOVAConfig_t*   config,
    _InOut_ struct DMAAddressSpace** spaceOut)
{
    struct DMAAddressSpace* space;
    oserr_t                 status;
    oserr_t                 cleanupStatus;

    if (spaceOut == NULL) {
        ERROR("DMAAddressSpaceCreateTranslated: translated space create called without an output pointer");
        return OS_EINVALPARAMS;
    }

    if (*spaceOut != NULL) {
        ERROR("DMAAddressSpaceCreateTranslated: translated space create called with an existing space %p", (void*)*spaceOut);
        return OS_EBUSY;
    }

    if (unit == NULL || config == NULL || config->Granularity != unit->Config.Granularity ||
        config->Bounds.Last > unit->Config.AddressLimit) {
        ERROR("DMAAddressSpaceCreateTranslated: translated space create called with unit %p and config %p that do not match",
              (void*)unit, (void*)config);
        return OS_EINVALPARAMS;
    }

    space = kmalloc(sizeof(struct DMAAddressSpace));
    if (space == NULL) {
        ERROR("DMAAddressSpaceCreateTranslated: failed to allocate a translated space for unit %p", (void*)unit);
        return OS_EOOM;
    }

    memset(space, 0, sizeof(struct DMAAddressSpace));
    atomic_init(&space->References, 1);
    space->Mode = DMAAddressSpaceTranslated;
    space->Translated.Unit = unit;

    // Count the space in the unit now, even though the caller cannot see it
    // yet. The allocator is built without holding Lock, and the unit must not
    // be destroyed in the meantime. If cleanup fails later, the count also
    // keeps the unit alive so the failure can be debugged.
    status = __CountSpaceInUnit(unit, space);
    if (status != OS_EOK) {
        kfree(space);
        return status;
    }

    status = DMAIOVAAllocatorCreate(config, &space->Translated.Allocator);
    if (status != OS_EOK) {
        ERROR("DMAAddressSpaceCreateTranslated: IOVA allocator create for a space on unit %p failed: %u", (void*)unit, status);
        goto cleanup;
    }

    status = __CreateBackendSpace(space);
    if (status != OS_EOK) {
        goto cleanup;
    }

    TRACE("DMAAddressSpaceCreateTranslated: translated space %p created on unit %p", (void*)space, (void*)unit);
    *spaceOut = space;
    return OS_EOK;

cleanup:
    cleanupStatus = DMATranslatedSpaceDestroy(space);
    if (cleanupStatus != OS_EOK) {
        ERROR("DMAAddressSpaceCreateTranslated: unpublished translated space %p cleanup failed: %u", (void*)space, cleanupStatus);
        return status;
    }
    kfree(space);
    return status;
}

/**
 * @brief Check the physical extents of a buffer and add up their lengths.
 *
 * A granule is the smallest block the IOMMU can map. Every extent must start
 * on a granule boundary, cover whole granules and stay below the physical
 * address limit of the unit.
 *
 * @param lengthOut Receives the total length of all extents in bytes.
 * @return OS_EOK, OS_EINVALPARAMS for an empty, unaligned or out of range
 *         extent, or OS_EOVERFLOW if the total does not fit in 64 bits.
 */
static oserr_t
__ValidateExtents(
    _In_  const DMAIOMMUUnit_t* unit,
    _In_  const SHMSG_t*        extents,
    _In_  int                   extentCount,
    _Out_ uint64_t*             lengthOut)
{
    uint64_t total = 0;
    uint64_t mask = unit->Config.Granularity - 1;

    for (int i = 0; i < extentCount; i++) {
        uint64_t physical = extents[i].Address;
        uint64_t length = extents[i].Length;

        // Only whole granules are accepted. The IOMMU cannot map part of a
        // granule, and rounding up would expose bytes the caller never gave
        // us. Whether the device should reach these bytes is the caller's call.
        if (length == 0 || (physical & mask) != 0 || (length & mask) != 0 ||
            physical > unit->Config.PhysicalLimit ||
            length - 1 > unit->Config.PhysicalLimit - physical) {
            WARNING("__ValidateExtents: extent %i at 0x%llx with length 0x%llx cannot be mapped by unit %p",
                    i, (unsigned long long)physical, (unsigned long long)length, (void*)unit);
            return OS_EINVALPARAMS;
        }

        if (length > UINT64_MAX - total) {
            WARNING("__ValidateExtents: extent %i makes the total length overflow", i);
            return OS_EOVERFLOW;
        }
        total += length;
    }

    *lengthOut = total;
    return OS_EOK;
}

// Allocate the generic mapping and the translated record behind it, and copy
// the extents. Nothing is reserved in the IOVA allocator or the backend yet.
// The mapping starts in the Prepared state with a single segment. The caller
// fills in the segment once the IOVA range is known.
static oserr_t
__CreateMappingRecord(
    _In_  struct DMAAddressSpace*   space,
    _In_  const SHMSG_t*            extents,
    _In_  int                       extentCount,
    _In_  uint64_t                  length,
    _In_  enum DMADirection         direction,
    _Out_ struct DMAAddressMapping** mappingOut)
{
    struct DMAAddressMapping* mapping;
    DMATranslatedMapping_t*   translated;
    size_t                    extentBytes;

    if ((size_t)extentCount > (SIZE_MAX - sizeof(*translated)) / sizeof(SHMSG_t)) {
        ERROR("__CreateMappingRecord: %i extents are too many for one mapping record", extentCount);
        return OS_EOVERFLOW;
    }

    extentBytes = (size_t)extentCount * sizeof(SHMSG_t);

    // The device sees the whole buffer as one continuous IOVA range, so the
    // mapping only needs room for a single segment.
    mapping = kmalloc(sizeof(struct DMAAddressMapping) + sizeof(struct DMASegment));
    if (mapping == NULL) {
        ERROR("__CreateMappingRecord: failed to allocate a mapping for space %p", (void*)space);
        return OS_EOOM;
    }

    translated = kmalloc(sizeof(DMATranslatedMapping_t) + extentBytes);
    if (translated == NULL) {
        ERROR("__CreateMappingRecord: failed to allocate a translated mapping of %u bytes for space %p",
              (unsigned int)(sizeof(DMATranslatedMapping_t) + extentBytes), (void*)space);
        kfree(mapping);
        return OS_EOOM;
    }

    memset(translated, 0, sizeof(DMATranslatedMapping_t));
    mapping->Space = space;
    mapping->State = DMAAddressMappingPrepared;
    mapping->Direction = direction;
    mapping->SegmentCount = 1;
    mapping->Translated = translated;
    memcpy(translated->Extents, extents, extentBytes);
    translated->Config.Length = length;
    translated->Config.Direction = direction;
    translated->Config.Extents = translated->Extents;
    translated->Config.ExtentCount = extentCount;

    *mappingOut = mapping;
    return OS_EOK;
}

// Let the backend build its record of the mapping. The unit health is checked
// before the call and once more after it, all while holding Lock. Nothing is
// made visible to the device here.
static oserr_t
__PrepareBackendMapping(
    _In_    struct DMAAddressSpace*  space,
    _InOut_ DMATranslatedMapping_t*  translated)
{
    DMAIOMMUUnit_t* unit = space->Translated.Unit;
    oserr_t         status;

    MutexLock(&unit->Lock);
    status = DMAAddressSpaceCheck(space);
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        WARNING("__PrepareBackendMapping: unit %p has failed, a mapping cannot be prepared: %u", (void*)unit, status);
        return status;
    }

    status = unit->Ops.PrepareMapping(
        space->Translated.BackendSpace,
        &translated->Config,
        &translated->BackendMapping
    );
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        ERROR("__PrepareBackendMapping: backend PrepareMapping at 0x%llx on unit %p failed: %u",
              (unsigned long long)translated->Config.DeviceAddress, (void*)unit, status);
        return status;
    }

    status = DMAAddressSpaceCheck(space);
    MutexUnlock(&unit->Lock);
    if (status != OS_EOK) {
        WARNING("__PrepareBackendMapping: unit %p failed while preparing a mapping: %u", (void*)unit, status);
    }
    return status;
}

// Free the backend record of a mapping, if the backend made one. After this the
// backend holds no pointers into the translated record.
static void
__DestroyBackendMapping(
    _In_ struct DMAAddressSpace* space,
    _In_ DMATranslatedMapping_t* translated)
{
    DMAIOMMUUnit_t* unit = space->Translated.Unit;

    if (translated->BackendMapping == NULL) {
        return;
    }

    MutexLock(&unit->Lock);
    unit->Ops.DestroyMapping(space->Translated.BackendSpace, translated->BackendMapping);
    MutexUnlock(&unit->Lock);
}

// Throw away a mapping that failed to prepare. Nothing was made visible to the
// device, even if the unit failed during a backend call. Cleanup follows the
// same path as a caller that drops a prepared mapping, and it needs no memory
// allocation.
static void
__DiscardPreparedMapping(
    _In_ struct DMAAddressMapping* mapping)
{
    DMATranslatedMapping_t* translated = mapping->Translated;
    oserr_t                 status;

    status = DMATranslatedMappingRelease(mapping);
    if (status != OS_EOK) {
        // Only the IOVA release can fail for a prepared mapping. The range
        // stays reserved and is lost, but the backend record was never
        // installed and can still be destroyed. Do that now, so the backend
        // holds no pointers into this memory when the caller drops its
        // reference on the space.
        ERROR("__DiscardPreparedMapping: unpublished IOVA allocation %p stranded: %u",
              (void*)translated->Allocation, status);
        __DestroyBackendMapping(mapping->Space, translated);
        kfree(translated);
    }
    kfree(mapping);
}

oserr_t
DMATranslatedMappingPrepare(
    _In_    struct DMAAddressSpace*          space,
    _In_    const SHMSG_t*                   extents,
    _In_    int                              extentCount,
    _In_    uint64_t                         addressLimit,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAAddressMapping**       mappingOut)
{
    struct DMAAddressMapping* mapping;
    DMATranslatedMapping_t*   translated;
    DMAIOVARequest_t          request = { 0 };
    const DMAIOVARange_t*     range;
    oserr_t                   status;

    status = DMAAddressSpaceCheck(space);
    if (status != OS_EOK) {
        WARNING("DMATranslatedMappingPrepare: space %p cannot prepare a mapping, its unit has failed: %u",
                (void*)space, status);
        return status;
    }

    status = __ValidateExtents(
        space->Translated.Unit,
        extents,
        extentCount,
        &request.Length
    );
    if (status != OS_EOK) {
        return status;
    }

    if (requirements != NULL) {
        request.Alignment = requirements->AddressAlignment;
        if (requirements->LengthAlignment != 0 &&
            (request.Length & (requirements->LengthAlignment - 1)) != 0) {
            // An alignment refusal describes the pages, so it is traced.
            // DMAAddressMappingPrepare leaves logging it to its caller.
            TRACE("DMATranslatedMappingPrepare: mapping length 0x%llx does not meet length alignment 0x%llx",
                  (unsigned long long)request.Length,
                  (unsigned long long)requirements->LengthAlignment);
            return OS_ENOTSUPPORTED;
        }
    }
    request.Limit = addressLimit;

    status = __CreateMappingRecord(
        space,
        extents,
        extentCount,
        request.Length,
        direction,
        &mapping
    );
    if (status != OS_EOK) {
        return status;
    }
    translated = mapping->Translated;

    // Reserve one continuous block of IOVA space for the whole buffer before
    // calling the backend. The backend may keep pointers to the config, so it
    // must not change from here on.
    status = DMAIOVAAllocate(
        space->Translated.Allocator,
        &request,
        &translated->Allocation
    );
    if (status != OS_EOK) {
        WARNING("DMATranslatedMappingPrepare: no IOVA range of 0x%llx bytes for space %p: %u",
                (unsigned long long)request.Length, (void*)space, status);
        goto cleanup;
    }

    range = DMAIOVAAllocationRange(translated->Allocation);
    translated->Config.DeviceAddress = range->First;
    mapping->Segments[0].Address = range->First;
    mapping->Segments[0].Length = request.Length;

    status = __PrepareBackendMapping(space, translated);
    if (status != OS_EOK) {
        goto cleanup;
    }

    *mappingOut = mapping;
    return OS_EOK;

cleanup:
    __DiscardPreparedMapping(mapping);
    return status;
}

oserr_t
DMATranslatedMappingInstall(
    _In_ struct DMAAddressMapping* mapping)
{
    struct DMAAddressSpace* space = mapping->Space;
    DMAIOMMUUnit_t*         unit = space->Translated.Unit;
    oserr_t                status;

    MutexLock(&unit->Lock);
    status = DMAAddressSpaceCheck(space);
    if (status != OS_EOK) {
        // The backend was not called, so the mapping is still in the prepared
        // state and can be discarded without touching hardware. A failed unit
        // never accepts new installs.
        MutexUnlock(&unit->Lock);
        WARNING("DMATranslatedMappingInstall: mapping at 0x%llx not installed, its unit %p has failed: %u",
                (unsigned long long)mapping->Translated->Config.DeviceAddress, (void*)unit, status);
        return status;
    }

    // Mark the install as started before the backend runs. If it fails or
    // times out part way, a later release knows the IOMMU may hold part of
    // this mapping and asks the backend to remove it.
    mapping->State = DMAAddressMappingInstallStarted;
    status = unit->Ops.InstallMapping(
        space->Translated.BackendSpace,
        mapping->Translated->BackendMapping,
        unit->Config.CompletionTimeoutNs
    );
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        ERROR("DMATranslatedMappingInstall: backend InstallMapping at 0x%llx on unit %p failed: %u",
              (unsigned long long)mapping->Translated->Config.DeviceAddress, (void*)unit, status);
        return status;
    }

    // A unit failure is logged once, when it is reported, so a failed check
    // below returns without a second message.
    status = DMAAddressSpaceCheck(space);
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        return status;
    }

    mapping->State = DMAAddressMappingInstalled;
    MutexUnlock(&unit->Lock);
    return OS_EOK;
}

// Ask the backend to remove an installed mapping, and wait until the IOMMU no
// longer uses the old translation. Called with no lock held. The state tells a
// later retry how far the removal got.
static oserr_t
__RemoveFromIOMMU(
    _InOut_ struct DMAAddressMapping* mapping)
{
    struct DMAAddressSpace* space = mapping->Space;
    DMAIOMMUUnit_t*         unit = space->Translated.Unit;
    oserr_t                 status;

    MutexLock(&unit->Lock);
    status = DMAAddressSpaceCheck(space);
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        WARNING("__RemoveFromIOMMU: mapping at 0x%llx not removed, its unit %p has failed: %u",
                (unsigned long long)mapping->Translated->Config.DeviceAddress, (void*)unit, status);
        return status;
    }

    mapping->State = DMAAddressMappingRemoving;
    status = unit->Ops.RemoveMapping(
        space->Translated.BackendSpace,
        mapping->Translated->BackendMapping,
        unit->Config.CompletionTimeoutNs
    );
    if (status == OS_EOK) {
        status = DMAAddressSpaceCheck(space);
    }
    if (status != OS_EOK) {
        MutexUnlock(&unit->Lock);
        ERROR("__RemoveFromIOMMU: removing the mapping at 0x%llx from unit %p failed: %u",
              (unsigned long long)mapping->Translated->Config.DeviceAddress, (void*)unit, status);
        return status;
    }

    // Record that the removal is done before the caller releases the IOVA
    // range. If the release fails and the caller retries, the backend removal
    // is not run a second time.
    mapping->State = DMAAddressMappingRemoved;
    MutexUnlock(&unit->Lock);
    return OS_EOK;
}

oserr_t
DMATranslatedMappingRelease(
    _In_ struct DMAAddressMapping* mapping)
{
    struct DMAAddressSpace* space = mapping->Space;
    DMATranslatedMapping_t* translated = mapping->Translated;
    oserr_t                 status;

    // A mapping that was only prepared, or is already removed, has nothing
    // in the IOMMU to undo. Otherwise remove it from the IOMMU first. The
    // device must have stopped using the IOVA range before this is called.
    if (mapping->State != DMAAddressMappingPrepared && mapping->State != DMAAddressMappingRemoved) {
        status = __RemoveFromIOMMU(mapping);
        if (status != OS_EOK) {
            return status;
        }
    }

    status = DMAIOVAAllocationRelease(&translated->Allocation);
    if (status != OS_EOK) {
        const DMAIOVARange_t* range = DMAIOVAAllocationRange(translated->Allocation);

        ERROR("DMA: IOVA release 0x%llx-0x%llx failed: %u",
              (unsigned long long)range->First, (unsigned long long)range->Last, status);
        return status;
    }

    __DestroyBackendMapping(space, translated);
    kfree(translated);
    mapping->Translated = NULL;
    return OS_EOK;
}
