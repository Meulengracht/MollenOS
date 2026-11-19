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
 * @brief Add a reference to a space after checking that this is still allowed.
 *
 * This gives no device access to anything. Call it while holding an existing
 * reference, or while holding the manager lock of a domain that is registered
 * and Ready. A domain in the Closing state does not keep its space allocated.
 * This also works after the unit has failed, so cleanup code can keep the
 * space allocated.
 *
 * @param space Address space to add a reference to.
 * @return OS_EOK, OS_EINVALPARAMS for NULL, OS_ECANCELLED if the count is zero
 *         because the space is being destroyed, or OS_EOVERFLOW if the count
 *         cannot go any higher.
 */
KERNELABI oserr_t KERNELAPI
DMAAddressSpaceAcquire(
    _In_ struct DMAAddressSpace* space);

/**
 * @brief Destroy a space, but only if the creator's reference is the last one.
 *
 * Before calling, stop other threads from finding the space and wait for calls
 * that use the creator's pointer to return. Any other reference, from a device
 * context or a mapping, makes this return OS_EBUSY. If cleanup fails, the
 * reference is put back and the pointer is kept, so the call can be retried.
 * No device is disconnected by this call.
 *
 * @param space Set to NULL on success. NULL or *space == NULL does nothing and succeeds.
 * @return OS_EOK, OS_EBUSY, or an error from the allocator or backend cleanup.
 */
KERNELABI oserr_t KERNELAPI
DMAAddressSpaceDestroy(
    _InOut_ struct DMAAddressSpace** space);

/**
 * @brief Get the granule size of a space. Hold a reference to the space first.
 *
 * A granule is the smallest block of memory the IOMMU can map. A direct space
 * does no translation, so it returns zero. A translated space returns the
 * granule size of its IOMMU unit. The value never changes.
 */
KERNELABI uint64_t KERNELAPI
DMAAddressSpaceGetGranularity(
    _In_ const struct DMAAddressSpace* space);

/**
 * @brief Get the highest physical address a space can map. Hold a reference to
 *        the space first.
 *
 * A translated space returns the inclusive physical limit of its IOMMU unit.
 * The unit cannot point a translation at memory above it, so preparing a
 * mapping of such memory fails. A direct space has no IOMMU unit and no such
 * limit, so it returns UINT64_MAX. What a direct device can reach is decided
 * by the space's address ranges instead. The value never changes.
 */
KERNELABI uint64_t KERNELAPI
DMAAddressSpaceGetPhysicalLimit(
    _In_ const struct DMAAddressSpace* space);

/**
 * @brief Check whether this space can accept new DMA work right now.
 *
 * Hold a space reference. Success only holds for this moment, because the unit
 * can fail right after the check. Code that starts a transfer must therefore
 * pair this check with the binding recovery gate. This function does not stop
 * devices and does not remove existing translations.
 *
 * @return OS_EOK for direct or healthy translated spaces, OS_ECANCELLED for a
 *         failed translated unit, or OS_EINVALPARAMS for NULL.
 */
KERNELABI oserr_t KERNELAPI
DMAAddressSpaceCheck(
    _In_ const struct DMAAddressSpace* space);

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
 * A translated space needs extents that cover whole granules. It returns one
 * segment of consecutive IOVAs with no padding. An IOVA is an address the
 * device uses, which the IOMMU translates to a physical address. The device can
 * reach every byte of each granule, so the caller must check that it may expose
 * all of those bytes to the device.
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
 *         allocations and references are released on failure. OS_ENOENT (the
 *         device cannot reach a page) and OS_ENOTSUPPORTED (an alignment rule
 *         is broken) are only traced. The caller must log them if they end the
 *         request.
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
 *         unchanged. OS_ECANCELLED if an earlier install or removal of this
 *         mapping failed. After any other failure, the mapping may only be
 *         released.
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
 * The segments describe every byte the mapping makes reachable. For a
 * translated mapping that is the complete span of whole granules. A caller
 * that maps a shorter request, such as SHM translated bounce storage, can
 * publish a smaller view of this span to the driver. This list is unchanged.
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
