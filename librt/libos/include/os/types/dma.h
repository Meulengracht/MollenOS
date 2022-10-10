/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __OS_TYPES_DMABUF_H__
#define __OS_TYPES_DMABUF_H__

#include <os/osdefs.h>

/**
 * Configuration flags for creation of a new dma buffer
 */
#define DMA_PERSISTANT  0x00000001U // Used to indicate underlying memory should not be freed upon dma destruction
#define DMA_UNCACHEABLE 0x00000002U // Used to indicate that the dma buffer should be disabled caching of the memory
#define DMA_CLEAN       0x00000004U // Zero out any allocated memory for the dma buffer
#define DMA_TRAP        0x00000008U // Dma region is a trap region. This can not be used in normal circumstances.

/**
 * Dma type buffer which can indicate which kind of memory will be allocated
 */
#define DMA_TYPE_REGULAR      0  // Regular allocation of physical memory
#define DMA_TYPE_DRIVER_ISA   1  // Memory should be located in ISA-compatable memory (<16mb)
#define DMA_TYPE_DRIVER_32LOW 2  // Memory should be allocated in a lower 32 bit region as device may not be fully 32 bit compliant
#define DMA_TYPE_DRIVER_32    3  // Memory should be located in 32 bit memory
#define DMA_TYPE_DRIVER_64    4  // Memory should be located in 64 bit memory

/**
 * Access flags that are available when mapping a dma buffer.
 * Read is always implied when mapping a region.
 */
#define DMA_ACCESS_WRITE   0x00000001U
#define DMA_ACCESS_EXECUTE 0x00000002U

typedef struct DMASG {
    uintptr_t address;
    size_t    length;
} DMASG_t;

typedef struct DMASGTable {
    DMASG_t* entries;
    int      count;
} DMASGTable_t;

typedef struct DMABuffer {
    const char*  name;
    size_t       length;
    size_t       capacity;
    unsigned int flags;
    unsigned int type;
} DMABuffer_t;

typedef struct DMAAttachment {
    uuid_t handle;
    void*  buffer;
    size_t length;
} DMAAttachment_t;

#endif //!__OS_TYPES_DMABUF_H__
