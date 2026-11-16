/**
 * Copyright 2026, Philip Meulengracht
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
 *
 * Shared DMA range, cache policy, and device description types used inside deviced.
 */

#ifndef __DEVICED_DMA_TYPES_H__
#define __DEVICED_DMA_TYPES_H__

#include <os/osdefs.h>

// Maximum number of address ranges a single DMA description can hold.
#define DM_DMA_MAX_RANGES 64

/**
 * @brief One contiguous block of memory a device can reach over DMA.
 * Some buses map device addresses differently from CPU physical addresses, so
 * each entry pairs the CPU physical start address with the matching bus address.
 */
typedef struct DmDmaRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
} DmDmaRange_t;

/**
 * @brief List of memory ranges a device is allowed to access for DMA.
 * Only the first Count entries in Ranges are valid, up to DM_DMA_MAX_RANGES.
 */
typedef struct DmDmaMap {
    uint32_t     Count;
    DmDmaRange_t Ranges[DM_DMA_MAX_RANGES];
} DmDmaMap_t;

enum DmDmaCachePolicy {
    // Cached RAM cannot be used for DMA yet because the bus does not know
    // whether the device sees CPU caches automatically.
    DmDmaCacheUnknown,
    // The CPU and device may see stale cached data, so software has to flush
    // CPU caches before device reads and invalidate them before CPU reads.
    DmDmaCacheNonCoherent,
    // Hardware keeps CPU caches in sync with DMA accesses, so software cache
    // flushes are skipped. Bus code should only pick this after checking every
    // bridge and bus hop between the device and memory.
    DmDmaCacheCoherent
};

/**
 * @brief Full DMA setup for a device: which device it belongs to, its parent
 * DMA host or IOMMU controller (HostId), the highest address the device can
 * target (AddressLimit), cache rules (CachePolicy), and allowed RAM ranges (Map).
 */
typedef struct DmDmaDescription {
    uuid_t                DeviceId;
    uuid_t                HostId;
    uint64_t              AddressLimit;
    enum DmDmaCachePolicy CachePolicy;
    DmDmaMap_t            Map;
} DmDmaDescription_t;

#endif //!__DEVICED_DMA_TYPES_H__
