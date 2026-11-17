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

// Which way data moves. The kernel uses it to decide which cache operations and
// copies are needed.
enum DMADirection {
    // The device only reads the memory, e.g. a USB OUT transfer.
    DMAToDevice,
    // The device only writes the memory, e.g. a USB IN transfer.
    DMAFromDevice,
    // The device may both read and write the mapped bytes.
    DMABidirectional
};

// Use the original SHM pages only. The kernel must not make a temporary copy
// (a bounce buffer) in memory the device can reach. Without this flag it may.
#define DMA_MAP_REQUIRE_DIRECT 0x1U
// Lets several device jobs use the mapping at once. The kernel counts the jobs.
// It prepares the CPU caches when the first job starts and again when the last
// job ends. Requires DMA_MAP_REQUIRE_DIRECT.
#define DMA_MAP_PERSISTENT     0x2U
// Maps one or more whole registered regions of an SHM_REGIONS buffer using
// their original pages. Each region is opened and synced through its own
// region session instead of sharing one job count across the whole mapping.
// Requires DMA_MAP_REQUIRE_DIRECT and cannot be combined with DMA_MAP_PERSISTENT.
#define DMA_MAP_REGIONS        0x4U

/**
 * @brief A run of consecutive bytes, as the device addresses them.
 *
 * Address is always 64-bit because a device address can exceed 4 GiB even on
 * a 32-bit kernel. It may differ from the CPU physical address stored in SHMSG_t,
 * for example when the bus adds an offset or an IOMMU translates addresses.
 */
struct DMASegment {
    uint64_t Address;
    uint64_t Length;
};

/**
 * @brief Limits on every device segment returned by a DMA mapping.
 *
 * Zero means no limit. Nonzero values must be powers of two. Segments are never
 * padded, so a nonzero LengthAlignment also applies to the total mapped length.
 * If a driver splits segments or adds offsets, the resulting addresses and
 * lengths must still meet these limits.
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

// Describes how a device sees the CPU caches. This decides whether the driver's
// begin and end calls have to flush caches, so it is fixed when the device is
// registered.
enum SHMDeviceCachePolicy {
    // Cache behavior is unknown. A DMA context cannot be opened for such a device.
    SHMDeviceCacheUnknown,
    // The hardware does not keep the CPU caches in step with the device. Cached
    // data must be written back before the device reads it, and dropped before
    // the CPU reads what the device wrote. The kernel does this in the begin and
    // end calls.
    SHMDeviceCacheNonCoherent,
    // The hardware keeps the CPU caches in step with device reads and writes, so
    // no cache work is needed. The driver still needs memory barriers to order
    // its own accesses.
    SHMDeviceCacheCoherent
};

// One range of CPU physical memory and the device address where the device sees
// it. Used when the two differ, for example because of a bus offset.
struct SHMDeviceRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
};

#endif //!__OS_TYPES_DMA_H__
