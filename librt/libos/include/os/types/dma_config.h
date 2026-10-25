/**
 * MollenOS
 *
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
 *
 */

#ifndef __OS_TYPES_DMA_CONFIG_H__
#define __OS_TYPES_DMA_CONFIG_H__

#include <os/types/dma.h>

#define DMA_MAX_RANGES 64

enum SHMDeviceCachePolicy {
    SHMDeviceCacheUnknown,
    SHMDeviceCacheNonCoherent,
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

struct OSDmaOpenParameters {
    uint64_t                  AddressLimit;
    struct OSDmaConfiguration Configuration;
};

#endif //!__OS_TYPES_DMA_CONFIG_H__
