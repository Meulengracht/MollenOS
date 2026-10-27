/**
 * Copyright 2017, Philip Meulengracht
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
 * DMA address ranges and cache policy passed from bus code to the device core.
 */

#ifndef __DEVICED_DEVICE_DMA_H__
#define __DEVICED_DEVICE_DMA_H__

#include <dma-types.h>

enum DmDmaCachePolicy {
    // Cached RAM cannot be used until a cache policy has been established.
    DmDmaCacheUnknown,
    // Software must synchronize caches before the CPU or device uses DMA data.
    DmDmaCacheNonCoherent,
    // Bus code must verify cache coherency for the complete device path.
    DmDmaCacheCoherent
};

/**
 * @brief Copied DMA description passed from bus code to the device core.
 *
 * DeviceId is filled by the core, so bus code cannot select another registry
 * entry. HostId identifies the registered bus owner within this service lifetime.
 * It is not a kernel handle or a driver permission. Map stores plain range
 * values: CPU physical base, device address base, and length.
 * These ranges describe addressability, not installed or allocated memory.
 */
struct DmDmaDescription {
    uuid_t                DeviceId;
    uuid_t                HostId;
    enum DmDmaCachePolicy CachePolicy;
    struct DmDmaMap       Map;
};

#endif
