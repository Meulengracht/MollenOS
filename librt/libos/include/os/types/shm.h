/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __OS_TYPES_SHM_H__
#define __OS_TYPES_SHM_H__

#include <os/osdefs.h>

/**
 * Configuration flags for creation of a new SHM region
 */
#define SHM_PERSISTANT  0x00000001U // Used to indicate underlying memory should not be freed upon dma destruction
#define SHM_UNCACHEABLE 0x00000002U // Used to indicate that the dma buffer should be disabled caching of the memory
#define SHM_CLEAN       0x00000004U // Zero out any allocated memory for the dma buffer
#define SHM_TRAP        0x00000008U // SHM region is a trap region. This can not be used in normal circumstances.
#define SHM_RESERVE     0x00000010U // SHM region is only reserved in memory
#define SHM_IPC         0x00000020U // SHM region is for IPC and needs a kernel copy.
#define SHM_STACK       0x00000040U // SHM region is intended for stack-use.
#define SHM_PRIVATE     0x00000080U // SHM region is intended for private use.

/**
 * SHM type which can indicate which kind of memory will be allocated
 */
#define SHM_TYPE_REGULAR      0  // Regular allocation of physical memory
#define SHM_TYPE_DRIVER_ISA   1  // Memory should be located in ISA-compatable memory (<16mb)
#define SHM_TYPE_DRIVER_32LOW 2  // Memory should be allocated in a lower 32 bit region as device may not be fully 32 bit compliant
#define SHM_TYPE_DRIVER_32    3  // Memory should be located in 32 bit memory
#define SHM_TYPE_DRIVER_64    4  // Memory should be located in 64 bit memory

/**
 * SHM Access flags that are available when mapping or attaching.
 * Read is always implied when mapping a region.
 */
#define SHM_ACCESS_WRITE   0x00000001U
#define SHM_ACCESS_EXECUTE 0x00000002U

/**
 * SHM attachment flags
 */
#define SHM_ATTACH_MAP 0x00000001U

typedef struct SHMSG {
    uintptr_t Address;
    size_t    Length;
} SHMSG_t;

typedef struct SHMSGTable {
    SHMSG_t* Entries;
    int      Count;
} SHMSGTable_t;

typedef struct SHM {
    const char*  Name;
    unsigned int Flags;
    unsigned int Type;
    size_t       Length;
    size_t       Capacity;
} SHM_t;

typedef struct SHMHandle {
    uuid_t Handle;
    void*  Buffer;
    size_t Length;
} SHMHandle_t;

#endif //!__OS_TYPES_DMABUF_H__
