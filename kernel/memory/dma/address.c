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
#include <limits.h>
#include <string.h>
#include "private.h"

/**
 * @brief Device segments and a reference to the address rules used to build them.
 *
 * The Segments array stays unchanged after creation. The caller must keep
 * the physical pages allocated until this mapping is released.
 */
struct DMAAddressMapping {
    struct DMAAddressSpace* Space;
    uint32_t                SegmentCount;
    struct DMASegment       Segments[];
};

static oserr_t
__ReferenceSpace(
    _In_ struct DMAAddressSpace* space)
{
    unsigned int references;

    // The caller must hold a reference while this mapping adds another.
    // That keeps the address space allocated during the count update.
    references = atomic_load(&space->References);
    do {
        if (references == UINT_MAX) {
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

oserr_t
DMAAddressSpaceCreateDirect(
    _In_    const struct SHMDeviceRange* ranges,
    _In_    uint32_t                     count,
    _InOut_ struct DMAAddressSpace**     spaceOut)
{
    struct DMAAddressSpace* space;
    size_t                  rangeBytes;
    oserr_t                 status;

    if (spaceOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*spaceOut != NULL) {
        return OS_EBUSY;
    }

    if (ranges == NULL || count == 0) {
        return OS_EINVALPARAMS;
    }

    if (count > DMA_MAX_RANGES) {
        return OS_ENOTSUPPORTED;
    }

    status = DMADirectValidateRanges(ranges, count);
    if (status != OS_EOK) {
        return status;
    }

    // DMA_MAX_RANGES keeps this allocation size within 32-bit limits. Copy the
    // ranges so the caller can change or free its array after this call.
    rangeBytes = count * sizeof(struct SHMDeviceRange);
    space = kmalloc(sizeof(struct DMAAddressSpace) + rangeBytes);
    if (space == NULL) {
        return OS_EOOM;
    }
    
    atomic_init(&space->References, 1);
    space->Count = count;
    memcpy(space->Ranges, ranges, rangeBytes);

    *spaceOut = space;
    return OS_EOK;
}

void
DMAAddressSpaceRelease(
    _InOut_ struct DMAAddressSpace** space)
{
    struct DMAAddressSpace* released;

    if (space == NULL || *space == NULL) {
        return;
    }

    // Clear the caller's pointer so repeated cleanup cannot decrement the
    // count again. Other references keep the address ranges available.
    released = *space;
    *space = NULL;
    if (atomic_fetch_sub(&released->References, 1) == 1) {
        kfree(released);
    }
}

oserr_t
DMAAddressMappingCreate(
    _In_    struct DMAAddressSpace*          space,
    _In_    const SHMSG_t*                   extents,
    _In_    int                              extentCount,
    _In_    uint64_t                         addressLimit,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAAddressMapping**       mappingOut)
{
    struct DMAAddressMapping* mapping;
    uint32_t                  segmentCount;
    size_t                    maxSegments;
    oserr_t                   status;

    if (mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (*mappingOut != NULL) {
        return OS_EBUSY;
    }

    if (space == NULL || extents == NULL || extentCount <= 0) {
        return OS_EINVALPARAMS;
    }

    if (!__RequirementsValid(requirements)) {
        return OS_EINVALPARAMS;
    }

    // Count and validate the segments before allocating the array. This only
    // converts addresses and does not change hardware address mappings.
    status = DMADirectWalkSegments(
        space,
        addressLimit,
        extents,
        extentCount,
        requirements,
        NULL,
        &segmentCount
    );
    if (status != OS_EOK) {
        return status;
    }

    maxSegments = (SIZE_MAX - sizeof(struct DMAAddressMapping)) / sizeof(struct DMASegment);
    if (segmentCount > maxSegments) {
        return OS_EOOM;
    }
    mapping = kmalloc(sizeof(struct DMAAddressMapping) + segmentCount * sizeof(struct DMASegment));
    if (mapping == NULL) {
        return OS_EOOM;
    }

    // The ranges and requirements must be unchanged since the count pass.
    // Finish the segment list before making it available to the caller.
    status = DMADirectWalkSegments(
        space,
        addressLimit,
        extents,
        extentCount,
        requirements,
        mapping->Segments,
        &segmentCount
    );
    if (status != OS_EOK) {
        kfree(mapping);
        return status;
    }

    status = __ReferenceSpace(space);
    if (status != OS_EOK) {
        kfree(mapping);
        return status;
    }

    mapping->Space = space;
    mapping->SegmentCount = segmentCount;

    *mappingOut = mapping;
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

    if (mapping == NULL || *mapping == NULL) {
        return OS_EOK;
    }

    // Direct mappings store a segment list and need no hardware cleanup.
    // SHM can release the physical pages after this call succeeds.
    released = *mapping;
    *mapping = NULL;
    
    DMAAddressSpaceRelease(&released->Space);
    kfree(released);
    return OS_EOK;
}
