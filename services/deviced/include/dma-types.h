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
 */

#ifndef __DEVICED_DMA_TYPES_H__
#define __DEVICED_DMA_TYPES_H__

#include <os/osdefs.h>

// Maximum number of device-to-physical-memory ranges in a description.
#define DM_DMA_MAX_RANGES 64

// One continuous device-address block that maps to physical memory.
typedef struct DmDmaRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
} DmDmaRange_t;

// List of device-to-physical-memory ranges available for DMA.
typedef struct DmDmaMap {
    uint32_t     Count;
    DmDmaRange_t Ranges[DM_DMA_MAX_RANGES];
} DmDmaMap_t;

enum DmDmaCachePolicy {
    // Cached RAM cannot be used until a cache policy has been established.
    DmDmaCacheUnknown,
    // Software must synchronize caches before the CPU or device uses DMA data.
    DmDmaCacheNonCoherent,
    // Bus code must verify cache coherency for the complete device path.
    DmDmaCacheCoherent
};

typedef struct DmDmaDescription {
    uuid_t                DeviceId;
    uuid_t                HostId;
    enum DmDmaCachePolicy CachePolicy;
    DmDmaMap_t            Map;
} DmDmaDescription_t;

#endif
