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

#include <fdt/dma.h>

// Deviced uses this map type for every bus, including buses with no device tree.
typedef FdtDmaMap_t DmDmaMap_t;

#define DM_DMA_MAX_RANGES FDT_DMA_MAX_RANGES

enum DmDmaCachePolicy {
    // Cached RAM cannot be used for DMA yet because the bus does not know
    // whether the device sees data still sitting in CPU caches.
    DmDmaCacheUnknown,
    // The device reads main RAM directly and skips CPU caches. Software has to
    // flush CPU caches before the device reads a buffer, and invalidate CPU
    // caches before the CPU reads what the device wrote.
    DmDmaCacheNonCoherent,
    // Hardware keeps CPU caches in sync with device DMA, so software skips
    // manual cache flushes. Bus code should pick this only after checking every
    // bridge and bus hop between the device and memory.
    DmDmaCacheCoherent
};

/**
 * @brief Complete DMA setup for one device.
 *
 * Identifies the device (DeviceId), its parent DMA host or IOMMU controller
 * (HostId), the highest address the device can target (AddressLimit), cache
 * synchronization rules (CachePolicy), and the reachable RAM ranges (Map).
 */
typedef struct DmDmaDescription {
    uuid_t                DeviceId;
    uuid_t                HostId;
    uint64_t              AddressLimit;
    enum DmDmaCachePolicy CachePolicy;
    DmDmaMap_t            Map;
} DmDmaDescription_t;

#endif //!__DEVICED_DMA_TYPES_H__
