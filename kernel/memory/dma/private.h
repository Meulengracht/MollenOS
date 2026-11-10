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

#ifndef __DMA_PRIVATE_H__
#define __DMA_PRIVATE_H__

#include <dma/address.h>
#include <stdatomic.h>

/**
 * @brief Fixed rules for converting physical addresses to device addresses.
 *
 * The creating context and each completed mapping hold a reference. The Ranges
 * array stays unchanged after creation. The caller stores the device address
 * limit separately.
 */
struct DMAAddressSpace {
    _Atomic(unsigned int) References;
    uint32_t              Count;
    struct SHMDeviceRange Ranges[];
};

/**
 * @brief Check address ranges before copying them or converting addresses.
 *
 * @param ranges Kernel array with at least count entries.
 * @param count Must already be checked to be between 1 and DMA_MAX_RANGES.
 * @return OS_EOK on success. OS_EINVALPARAMS for an empty range, physical or
 *         device address overflow, or overlapping device ranges.
 */
oserr_t
DMADirectValidateRanges(
    _In_ const struct SHMDeviceRange* ranges,
    _In_ uint32_t                     count);

/**
 * @brief Find one device address range that covers all the requested bytes.
 *
 * @param space Address space. Hold a reference until the call returns.
 * @param addressLimit Largest device address allowed, inclusive.
 * @param physicalBase Physical address of the first byte.
 * @param length Nonzero number of bytes.
 * @param deviceAddressOut Receives the lowest device address that covers the
 *                         entire range within addressLimit.
 * @return OS_EOK on success. OS_EINVALPARAMS for invalid input or OS_ENOENT if
 *         no device range can address all the bytes. Output is unchanged on failure.
 */
oserr_t
DMADirectTranslate(
    _In_  const struct DMAAddressSpace* space,
    _In_  uint64_t                      addressLimit,
    _In_  uint64_t                      physicalBase,
    _In_  uint64_t                      length,
    _Out_ uint64_t*                     deviceAddressOut);

/**
 * @brief Count device segments or write them into a caller-supplied array.
 *
 * Used when creating direct mappings and answering SHM address queries. Before
 * filling the array, count the segments with the same inputs and provide room
 * for that many entries. Keep the inputs unchanged between calls.
 * The caller must check the pointers and extentCount, and check that each
 * nonzero alignment requirement is a power of two.
 *
 * @param space Address space. Hold a reference throughout both calls.
 * @param addressLimit Largest device address allowed, inclusive.
 * @param extents Allocated physical byte ranges in buffer order.
 * @param extentCount Number of entries in extents, at least one.
 * @param requirements Optional alignment requirements, checked by the caller.
 * @param segmentsOut NULL to count only, otherwise receives the device segments.
 * @param countOut Receives the number of segments on success.
 * @return OS_EOK on success. OS_EINVALPARAMS for an invalid physical range.
 *         OS_ENOENT if a byte has no device address. OS_ENOTSUPPORTED for unmet
 *         alignment or a segment count that does not fit in uint32_t.
 *         Discard partial output on failure. Neither call allocates memory or
 *         changes hardware address mappings.
 */
oserr_t
DMADirectWalkSegments(
    _In_  const struct DMAAddressSpace*    space,
    _In_  uint64_t                         addressLimit,
    _In_  const SHMSG_t*                   extents,
    _In_  int                              extentCount,
    _In_  const struct DMAMapRequirements* requirements,
    _Out_ struct DMASegment*               segmentsOut,
    _Out_ uint32_t*                        countOut);

#endif //!__DMA_PRIVATE_H__
