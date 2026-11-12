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

#ifndef __DMA_ADDRESS_H__
#define __DMA_ADDRESS_H__

#include <os/types/dma.h>

struct DMAAddressSpace;
struct DMAAddressMapping;

/**
 * @brief Validate fixed physical-to-device ranges without allocating an address space.
 *
 * @param ranges Kernel array with count entries, unchanged until this call returns.
 * @param count Number of entries, from 1 through DMA_MAX_RANGES.
 * @return OS_EOK, OS_EINVALPARAMS for NULL/empty ranges, endpoint overflow or
 * overlapping device ranges, or OS_ENOTSUPPORTED for too many entries.
 * Physical aliases are permitted. No memory is allocated and no output is changed.
 */
oserr_t
DMAAddressRangesValidate(
    _In_ const struct SHMDeviceRange* ranges,
    _In_ uint32_t                     count);

/**
 * @brief Validate and copy fixed physical-to-device address ranges.
 *
 * @param ranges Array in kernel memory. Keep its entries unchanged until this
 *               call returns.
 * @param count Number of ranges, from 1 through DMA_MAX_RANGES.
 * @param spaceOut Address of an initially NULL pointer. Receives the address
 *                 space with one reference, to release with DMAAddressSpaceRelease.
 * @return OS_EOK on success. OS_EINVALPARAMS for NULL arguments, empty ranges,
 *         address overflow or overlapping device ranges.
 *         OS_ENOTSUPPORTED for too many ranges. OS_EBUSY if *spaceOut is not NULL.
 *         OS_EOOM on allocation failure. Failure leaves *spaceOut unchanged.
 */
oserr_t
DMAAddressSpaceCreateDirect(
    _In_    const struct SHMDeviceRange* ranges,
    _In_    uint32_t                     count,
    _InOut_ struct DMAAddressSpace**     spaceOut);

/**
 * @brief Release one address-space reference and set the caller's pointer to NULL.
 *
 * The address space is freed after the last reference is released. Each live
 * mapping holds a separate reference.
 *
 * @param space Pointer to the reference to release. NULL or *space == NULL
 *              requires no work.
 */
void
DMAAddressSpaceRelease(
    _InOut_ struct DMAAddressSpace** space);

/**
 * @brief Build device segments for allocated physical ranges in buffer order.
 *
 * The caller must keep the physical pages allocated until release succeeds.
 * Keep extents and requirements unchanged throughout this call because they
 * are read twice: once to count the segments and once to fill the array.
 * The caller must separately check permission to use the memory and
 * synchronize CPU caches. Creating the segment list does neither.
 *
 * @param space Address space. Hold a reference until the call returns.
 * @param extents Kernel array describing the buffer's allocated physical bytes.
 * @param extentCount Number of entries in extents, at least one.
 * @param addressLimit Largest device address allowed, inclusive.
 * @param requirements Optional alignment requirements for each device segment.
 * @param mappingOut Address of an initially NULL pointer. Receives the completed mapping.
 * @return OS_EOK on success. Validation, address conversion, reference-count
 *         and allocation errors leave *mappingOut unchanged. Temporary
 *         allocations and references are released on failure.
 */
oserr_t
DMAAddressMappingCreate(
    _In_    struct DMAAddressSpace*          space,
    _In_    const SHMSG_t*                   extents,
    _In_    int                              extentCount,
    _In_    uint64_t                         addressLimit,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAAddressMapping**       mappingOut);

/**
 * @brief Get the device segment list from a completed mapping.
 *
 * @param mapping Valid mapping. Release must not overlap this call.
 * @param countOut Receives the number of segments.
 * @return The stored array, unchanged and valid until release starts.
 *         This call allocates no memory and does not access hardware.
 */
const struct DMASegment*
DMAAddressMappingSegments(
    _In_  const struct DMAAddressMapping* mapping,
    _Out_ uint32_t*                       countOut);

/**
 * @brief Release the device segment list and its address-space reference.
 *
 * Stop all device use before calling. Calls on this mapping must not overlap.
 *
 * @param mapping Pointer set to NULL only after successful release.
 *                NULL or *mapping == NULL requires no work. On failure, keep
 *                the physical pages allocated and retry release. Do not start
 *                more work through the mapping.
 * @return OS_EOK, or a release error that leaves the mapping available for
 *         another release attempt. Direct mappings need no hardware cleanup
 *         and always return OS_EOK.
 */
oserr_t
DMAAddressMappingRelease(
    _InOut_ struct DMAAddressMapping** mapping);

#endif //!__DMA_ADDRESS_H__
