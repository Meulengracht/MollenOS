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
 * @brief Work out the device segments for a buffer, without letting the device use them yet.
 *
 * The caller must keep the physical pages allocated until release succeeds.
 * Keep extents and requirements unchanged throughout this call because they
 * are read twice: once to count the segments and once to fill the array.
 * The caller must separately check permission to use the memory and
 * synchronize CPU caches. Preparation does neither. The input arrays are only
 * needed during this call. The mapping keeps its own reference to the address
 * space.
 *
 * @param space Address space. Hold a reference until the call returns.
 * @param extents Kernel array describing the buffer's allocated physical bytes.
 * @param extentCount Number of entries in extents, at least one.
 * @param addressLimit Largest device address allowed, inclusive.
 * @param direction DMAToDevice lets the device read, DMAFromDevice lets the
 *                  device write, and DMABidirectional allows both. Direct
 *                  DMA only records this. The hardware cannot enforce it.
 * @param requirements Optional alignment requirements for each device segment.
 * @param mappingOut Address of an initially NULL pointer. Receives the mapping in
 *                   the Prepared state.
 * @return OS_EOK on success. Validation, address conversion, reference-count
 *         and allocation errors leave *mappingOut unchanged. Temporary
 *         allocations and references are released on failure.
 */
KERNELABI oserr_t KERNELAPI
DMAAddressMappingPrepare(
    _In_    struct DMAAddressSpace*          space,
    _In_    const SHMSG_t*                   extents,
    _In_    int                              extentCount,
    _In_    uint64_t                         addressLimit,
    _In_    enum DMADirection                direction,
    _In_    const struct DMAMapRequirements* requirements,
    _InOut_ struct DMAAddressMapping**       mappingOut);

/**
 * @brief Let the device use a prepared mapping. This can be done only once.
 *
 * Before calling, do all the bookkeeping that is needed to clean up the mapping
 * if this call fails. Once installation has started, an error does not prove
 * that the device cannot reach the memory. After a failure, keep the mapping
 * and its pages until the device is confirmed stopped, then release it.
 * Do not call Install and Release on the same mapping at the same time.
 *
 * @param mapping Prepared mapping whose physical pages are still allocated.
 * @return OS_EOK once the device addresses are usable. OS_EINVALPARAMS for NULL.
 *         OS_EBUSY if the mapping is already installed. The mapping is then
 *         unchanged. After any other failure, the mapping may only be released.
 *         Direct mappings allocate nothing here and cannot fail.
 */
KERNELABI oserr_t KERNELAPI
DMAAddressMappingInstall(
    _In_ struct DMAAddressMapping* mapping);

/**
 * @brief Get the device segments of a Prepared or Installed mapping.
 *
 * The kernel uses this to check the layout. Reading the addresses does not
 * allow the device to use them. That only happens after Install succeeds.
 *
 * @param mapping Valid mapping. Keep it alive for as long as the array is used.
 * @param countOut Receives the number of segments.
 * @return The stored array, unchanged and valid until release starts.
 *         This call allocates no memory and does not access hardware.
 */
KERNELABI KERNELAPI const struct DMASegment*
DMAAddressMappingSegments(
    _In_  const struct DMAAddressMapping* mapping,
    _Out_ uint32_t*                       countOut);

/**
 * @brief Release the device segment list and its address-space reference.
 *
 * A Prepared mapping only needs software cleanup, which cannot fail. Once
 * installation has started, wait for all device work to finish, or confirm that
 * the hardware has stopped, before calling. Calls on this mapping must not
 * overlap. A successful release means the addresses and the storage behind them
 * can be reused, so any hardware cleanup must be finished by then.
 *
 * @param mapping Pointer set to NULL only after successful release.
 *                NULL or *mapping == NULL requires no work. On failure, keep
 *                the physical pages allocated and retry release. Do not start
 *                more work through the mapping.
 * @return OS_EOK, or a release error that leaves the mapping available for
 *         another release attempt. Direct mappings need no hardware cleanup
 *         and always return OS_EOK.
 */
KERNELABI oserr_t KERNELAPI
DMAAddressMappingRelease(
    _InOut_ struct DMAAddressMapping** mapping);

#endif //!__DMA_ADDRESS_H__
