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

#include <heap.h>
#include <debug.h>
#include <limits.h>
#include "private.h"

oserr_t
DMAAddressSpaceAcquire(
    _In_ struct DMAAddressSpace* space)
{
    unsigned int references;

    // The space must stay allocated during this call, which needs an existing
    // reference or a locked Ready domain. A count of zero means the creator is
    // destroying the space, so no new reference may be added.
    if (space == NULL) {
        ERROR("DMAAddressSpaceAcquire: address space acquire called without a space");
        return OS_EINVALPARAMS;
    }

    references = atomic_load(&space->References);
    do {
        if (references == 0) {
            WARNING("DMAAddressSpaceAcquire: address space %p is being destroyed, cannot acquire", (void*)space);
            return OS_ECANCELLED;
        }

        if (references == UINT_MAX) {
            ERROR("DMAAddressSpaceAcquire: address space %p reference count overflow", (void*)space);
            return OS_EOVERFLOW;
        }
    } while (!atomic_compare_exchange_weak(&space->References, &references, references + 1));
    return OS_EOK;
}

static bool
__RequirementsValid(
    _In_ const struct DMAMapRequirements* requirements)
{
    uint64_t alignment;

    if (requirements == NULL) {
        return true;
    }

    // Alignment checks use bit masks, which require powers of two.
    // Zero disables the requirement.
    alignment = requirements->AddressAlignment;
    if ((alignment & (alignment - 1)) != 0) {
        return false;
    }

    alignment = requirements->LengthAlignment;
    return (alignment & (alignment - 1)) == 0;
}

void
DMAAddressSpaceRelease(
    _InOut_ struct DMAAddressSpace** space)
{
    struct DMAAddressSpace* released;
    oserr_t                 status;

    if (space == NULL || *space == NULL) {
        return;
    }

    // Clear the caller's pointer so repeated cleanup cannot decrement the
    // count again. Other references keep the address ranges available.
    released = *space;
    *space = NULL;

    if (atomic_fetch_sub(&released->References, 1) == 1) {
        if (released->Mode == DMAAddressSpaceTranslated) {
            status = DMATranslatedSpaceDestroy(released);
            if (status != OS_EOK) {
                // This function returns nothing, so the error can only be
                // logged. Leave the space and its unit allocated, because
                // something may still point to them.
                ERROR("DMAAddressSpaceRelease: final address-space cleanup %p failed: %u", (void*)released, status);
                return;
            }
        }
        kfree(released);
    }
}

oserr_t
DMAAddressSpaceDestroy(
    _InOut_ struct DMAAddressSpace** space)
{
    struct DMAAddressSpace* released;
    unsigned int            expected = 1;
    oserr_t                 status;

    if (space == NULL || *space == NULL) {
        return OS_EOK;
    }

    // Keep a local copy of the pointer. The caller's pointer is only cleared
    // once destruction has succeeded, so a failed call can be retried.
    released = *space;

    // Change the count from 1 to 0. This only works when the creator's
    // reference is the last one, and a count of zero stops
    // DMAAddressSpaceAcquire from adding new references. The creator must
    // already prevent other threads from finding this space.
    if (!atomic_compare_exchange_strong(&released->References, &expected, 0)) {
        WARNING("DMAAddressSpaceDestroy: address space %p still has %u references", (void*)released, expected);
        return OS_EBUSY;
    }
    
    if (released->Mode == DMAAddressSpaceTranslated) {
        status = DMATranslatedSpaceDestroy(released);
        if (status != OS_EOK) {
            ERROR("DMAAddressSpaceDestroy: address space %p cleanup failed: %u", (void*)released, status);
            atomic_store(&released->References, 1);
            return status;
        }
    }

    kfree(released);
    *space = NULL;
    return OS_EOK;
}

uint64_t
DMAAddressSpaceGetGranularity(
    _In_ const struct DMAAddressSpace* space)
{
    return space->Mode == DMAAddressSpaceTranslated 
        ? space->Translated.Unit->Config.Granularity : 0;
}

oserr_t
DMAAddressSpaceCheck(
    _In_ const struct DMAAddressSpace* space)
{
    if (space == NULL) {
        ERROR("DMAAddressSpaceCheck: address space check called without a space");
        return OS_EINVALPARAMS;
    }

    if (space->Mode == DMAAddressSpaceTranslated &&
        atomic_load(&space->Translated.Unit->Failure) != OS_EOK) {
        WARNING("DMAAddressSpaceCheck: address space %p belongs to a failed IOMMU unit", (void*)space);
        return OS_ECANCELLED;
    }
    return OS_EOK;
}

oserr_t
DMAAddressMappingPrepare(
    _In_    struct DMAAddressSpace*          space,
    _In_    const SHMSG_t*                   extents,
    _In_    int                              extentCount,
    _In_    uint64_t                         addressLimit,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAAddressMapping**       mappingOut)
{
    struct DMAAddressMapping* mapping = NULL;
    oserr_t                   status;

    if (mappingOut == NULL) {
        ERROR("DMAAddressMappingPrepare: mapping prepare called without an output pointer");
        return OS_EINVALPARAMS;
    }

    if (*mappingOut != NULL) {
        ERROR("DMAAddressMappingPrepare: mapping prepare called with an existing mapping %p", (void*)*mappingOut);
        return OS_EBUSY;
    }

    if (space == NULL || extents == NULL || extentCount <= 0) {
        ERROR("DMAAddressMappingPrepare: invalid mapping prepare space %p, extents %p, count %i",
              (void*)space, (const void*)extents, extentCount);
        return OS_EINVALPARAMS;
    }

    if (direction != DMAToDevice && direction != DMAFromDevice && direction != DMABidirectional) {
        ERROR("DMAAddressMappingPrepare: invalid mapping direction %i", (int)direction);
        return OS_EINVALPARAMS;
    }

    if (!__RequirementsValid(requirements)) {
        ERROR("DMAAddressMappingPrepare: mapping alignment requirements are not powers of two");
        return OS_EINVALPARAMS;
    }

    // The mapping gets its own space reference before the helper runs. If the
    // helper fails, only that new reference is dropped and the caller's
    // reference stays as it was.
    status = DMAAddressSpaceAcquire(space);
    if (status != OS_EOK) {
        ERROR("DMAAddressMappingPrepare: failed to reference address space %p: %u", (void*)space, status);
        return status;
    }

    if (space->Mode == DMAAddressSpaceDirect) {
        status = DMADirectMappingPrepare(
            space,
            extents,
            extentCount,
            addressLimit,
            direction,
            requirements,
            &mapping
        );
    } else {
        status = DMATranslatedMappingPrepare(
            space,
            extents,
            extentCount,
            addressLimit,
            direction,
            requirements,
            &mapping
        );
    }
    if (status != OS_EOK) {
        ERROR("DMAAddressMappingPrepare: mapping prepare on space %p failed: %u", (void*)space, status);
        DMAAddressSpaceRelease(&space);
        return status;
    }

    *mappingOut = mapping;
    return OS_EOK;
}

oserr_t
DMAAddressMappingInstall(
    _In_ struct DMAAddressMapping* mapping)
{
    oserr_t status;

    if (mapping == NULL) {
        ERROR("DMAAddressMappingInstall: mapping install called without a mapping");
        return OS_EINVALPARAMS;
    }

    if (mapping->State != DMAAddressMappingPrepared) {
        if (mapping->State == DMAAddressMappingInstalled) {
            ERROR("DMAAddressMappingInstall: mapping %p is already installed", (void*)mapping);
            return OS_EBUSY;
        }
        ERROR("DMAAddressMappingInstall: mapping %p is in state %i and cannot be installed",
              (void*)mapping, (int)mapping->State);
        return OS_ECANCELLED;
    }

    if (mapping->Space->Mode == DMAAddressSpaceTranslated) {
        status = DMATranslatedMappingInstall(mapping);
        if (status != OS_EOK) {
            ERROR("DMAAddressMappingInstall: translated mapping %p install failed: %u", (void*)mapping, status);
        }
        return status;
    }

    // Direct ranges need no table updates or allocation. Keep the explicit
    // install step so both modes follow the same mapping lifetime.
    mapping->State = DMAAddressMappingInstalled;
    return OS_EOK;
}

const struct DMASegment*
DMAAddressMappingSegments(
    _In_  const struct DMAAddressMapping* mapping,
    _Out_ uint32_t*                       countOut)
{
    *countOut = mapping->SegmentCount;
    return mapping->Segments;
}

oserr_t
DMAAddressMappingRelease(
    _InOut_ struct DMAAddressMapping** mapping)
{
    struct DMAAddressMapping* released;
    oserr_t                   status;

    if (mapping == NULL || *mapping == NULL) {
        return OS_EOK;
    }

    released = *mapping;
    if (released->Space->Mode == DMAAddressSpaceTranslated) {
        status = DMATranslatedMappingRelease(released);
        if (status != OS_EOK) {
            ERROR("DMAAddressMappingRelease: translated mapping %p release failed: %u", (void*)released, status);
            return status;
        }
    }

    // After translated cleanup succeeds, nothing in the hardware tables or
    // command queue refers to this mapping or its memory any more. Only then
    // may SHM free the backing pages and the caller forget this object.
    DMAAddressSpaceRelease(&released->Space);
    kfree(released);
    *mapping = NULL;
    return OS_EOK;
}
