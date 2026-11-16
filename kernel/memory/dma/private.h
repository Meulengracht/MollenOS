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
 * @brief Describes which physical memory a device can reach, and at which
 *        device addresses.
 *
 * Each range maps a block of physical addresses to a block of device
 * addresses. The context that created this and every mapping made from it
 * (Prepared or Installed) each hold a reference. It is freed when the last
 * one is dropped.
 * The Ranges array never changes after creation. The highest device address
 * the device can use is not stored here. The caller keeps it separately.
 */
struct DMAAddressSpace {
    _Atomic(unsigned int) References;
    uint32_t              Count;
    struct SHMDeviceRange Ranges[];
};

/**
 * @brief Count device segments or write them into a caller-supplied array.
 *
 * A device segment is a run of bytes with consecutive device addresses, so the
 * device can transfer it as one block. This splits the physical ranges where
 * the device address jumps, and merges pieces that line up.
 *
 * Call it twice. First pass NULL for segmentsOut to get the count, then call
 * again with an array that has room for that many entries. Both calls must get
 * the same inputs. Callers use this when creating direct mappings and when
 * checking segment alignment. The caller must check the pointers and
 * extentCount, and check that each nonzero alignment requirement is a power
 * of two. This function does not.
 *
 * @param space Address space. Hold a reference throughout both calls.
 * @param addressLimit Highest device address allowed, inclusive.
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
