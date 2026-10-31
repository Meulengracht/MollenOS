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

/**
 * @brief One continuous device-address block that maps to physical memory.
 *
 * DeviceBase is an address the device can use, and PhysicalBase is the
 * corresponding address in physical memory. Length gives the number of bytes
 * covered. This describes where DMA could reach; it does not say that memory
 * has been allocated, that the full buffer fits, or that DMA hardware is ready.
 */
typedef struct DmDmaRange {
    uint64_t PhysicalBase;
    uint64_t DeviceBase;
    uint64_t Length;
} DmDmaRange_t;

/**
 * @brief List of device-to-physical-memory ranges available for DMA.
 *
 * The ranges are copied into this structure and sorted by DeviceBase. Their
 * device-address blocks do not overlap. Two different device-address blocks
 * can still refer to some or all of the same physical memory. Adjacent entries
 * are kept separate when the address mapping changes at their boundary.
 */
typedef struct DmDmaMap {
    uint32_t     Count;
    DmDmaRange_t Ranges[DM_DMA_MAX_RANGES];
} DmDmaMap_t;

#endif
