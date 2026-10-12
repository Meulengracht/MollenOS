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

// Which way data moves during a transfer. Cache handling and bounce copies use
// this to skip work that cannot matter for that direction.
enum SHMDeviceDirection {
    // The device only reads the memory, e.g. a USB OUT transfer.
    SHMDeviceToDevice,
    // The device only writes the memory, e.g. a USB IN transfer.
    SHMDeviceFromDevice,
    // The device both reads and writes, e.g. a controller's own structures.
    SHMDeviceBidirectional
};

/**
 * Unlike SHMSG_t, which holds CPU physical addresses in a pointer-sized type,
 * this always uses 64 bits because device addresses can exceed 4 GiB even on
 * 32-bit kernels, and they need not equal the physical address.
 */
struct SHMDeviceSegment {
    uint64_t Address;
    uint64_t Length;
};

/**
 * What a device needs from memory it keeps using, such as a controller's rings.
 * Zero for Alignment or Boundary means no requirement.
 */
struct SHMDeviceRequirements {
    // Number of bytes needed; the allocation is rounded up to whole pages.
    size_t   Length;
    // The device address of the first byte must be a multiple of this power of two.
    uint64_t Alignment;
    // No segment may cross a multiple of this power of two, e.g. 64 KiB for xHCI.
    uint64_t Boundary;
    // The device must see the whole allocation as a single segment.
    bool     Contiguous;
};

#endif //!__OS_TYPES_DMA_H__
