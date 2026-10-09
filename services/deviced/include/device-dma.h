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
 * Device descriptions and the service code that handles their requests.
 */

#ifndef __DEVICED_DEVICE_DMA_H__
#define __DEVICED_DEVICE_DMA_H__

#include <firmware/dma.h>

enum DmDmaCachePolicy {
    // No policy has been established; this does not permit using cached RAM.
    DmDmaCacheUnknown,
    // Software must arrange visibility before handing memory to the other side.
    DmDmaCacheNonCoherent,
    // A provider must establish this guarantee for the complete device path.
    DmDmaCacheCoherent
};

/**
 * @brief Copied DMA description shared by providers and the device core.
 *
 * DeviceId is filled by the core, so a provider cannot select another registry
 * entry. HostId identifies the registered bus owner within this service lifetime;
 * it is not a kernel handle or a driver permission. Map reuses the existing plain
 * range values: CPU physical base, device address base, and length. Despite its
 * firmware name, FdtDmaMap contains no firmware pointers or parsing operations.
 * These ranges describe addressability, not installed or allocated memory.
 */
struct DmDmaDescription {
    uuid_t                DeviceId;
    uuid_t                HostId;
    enum DmDmaCachePolicy CachePolicy;
    struct FdtDmaMap      Map;
};

#endif
