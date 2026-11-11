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

#ifndef __OS_TYPES_DMA_H__
#define __OS_TYPES_DMA_H__

#include <os/osdefs.h>
#include <os/types/shm.h>

#define DMA_MAX_RANGES 64

// Which way data moves. Determines which cache operations and copies are needed.
enum DMADirection {
    // The device only reads the memory, e.g. a USB OUT transfer.
    DMAToDevice,
    // The device only writes the memory, e.g. a USB IN transfer.
    DMAFromDevice,
    // The device may read and write the mapped transfer bytes.
    DMABidirectional
};

// Use the original SHM pages. Temporary bounce copies are not allowed.
#define DMA_MAP_REQUIRE_DIRECT 0x1U
// Count device uses and synchronize caches before the first and after the last.
// Requires DMA_MAP_REQUIRE_DIRECT.
#define DMA_MAP_PERSISTENT     0x2U

/**
 * @brief A consecutive range of byte addresses used by a device.
 *
 * Address is always 64-bit because a device address can exceed 4 GiB even on
 * a 32-bit kernel. It may differ from the CPU physical address stored in SHMSG_t.
 */
struct DMASegment {
    uint64_t Address;
    uint64_t Length;
};

/**
 * @brief Constraints on every device segment returned by a DMA mapping.
 *
 * Zero means no constraint. Nonzero values must be powers of two.
 * A nonzero LengthAlignment also applies to the total mapped length, which is
 * never padded. When drivers split segments or add offsets, the resulting
 * addresses and lengths must still meet these requirements.
 */
struct DMAMapRequirements {
    // The device address of each segment must be a multiple of this value.
    uint64_t AddressAlignment;
    // The length of each segment must be a multiple of this value.
    uint64_t LengthAlignment;
};

/**
 * @brief Memory requirements for long-lived device buffers, such as command rings.
 *
 * Zero for Alignment or Boundary means no requirement.
 */
struct DMARequirements {
    // Number of bytes needed. The allocation is rounded up to whole pages.
    size_t   Length;
    // The device address of the first byte must be a multiple of this power of two.
    uint64_t Alignment;
    // No segment may cross a multiple of this power of two, e.g. 64 KiB for xHCI.
    uint64_t Boundary;
    // The device must see the whole allocation as a single segment.
    bool     Contiguous;
};

enum SHMDeviceCachePolicy {
    // Cache behavior is unknown. A DMA context cannot use this policy.
    SHMDeviceCacheUnknown,
    // Software must write back or invalidate cached data for DMA.
    SHMDeviceCacheNonCoherent,
    // Hardware keeps CPU caches consistent with device reads and writes.
    // Memory barriers are still needed to order accesses.
    SHMDeviceCacheCoherent
};

struct SHMDeviceRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
};

struct OSDmaConfiguration {
    uint64_t                  Session;
    uint32_t                  Count;
    enum SHMDeviceCachePolicy CachePolicy;
    struct SHMDeviceRange     Ranges[DMA_MAX_RANGES];
};

#endif //!__OS_TYPES_DMA_H__
