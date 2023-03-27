/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Memory Type Definitions & Structures
 * - This header describes the base memory-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __TYPES_MEMORY_H__
#define __TYPES_MEMORY_H__

#include <os/osdefs.h>

// Memory Allocation Definitions
// Flags that can be used when requesting virtual memory
#define MEMORY_COMMIT        0x00000001U                  // If commit is not passed, memory will only be reserved.
#define MEMORY_CLEAN         0x00000002U                  // Memory should be cleaned
#define MEMORY_UNCHACHEABLE  0x00000004U                  // Memory must not be cached
#define MEMORY_CLONE         0x00000008U                  // Clone the memory mapping passed in as hint
#define MEMORY_FIXED         0x00000010U                  // Use the value provided in Hint
#define MEMORY_STACK         0x00000020U                  // Memory is used as a stack, and should grow downwards

#define MEMORY_READ          0x00000100U                  // Memory is readable
#define MEMORY_WRITE         0x00000200U                  // Memory is writable
#define MEMORY_EXECUTABLE    0x00000400U                  // Memory is executable
#define MEMORY_DIRTY         0x00000800U                  // Memory is dirty

enum OSMemoryConformity {
    // No memory conformity required
    OSMEMORYCONFORMITY_NONE,
    // Legacy memory conformity required. For x86 system this
    // means no memory above 16mb is valid.
    OSMEMORYCONFORMITY_LEGACY,
    // Low memory conformity. This means that memory must be
    // located in the low 32 bits area for it to be valid.
    OSMEMORYCONFORMITY_LOW,
    // 32 bit memory conformity. Any memory located in the 32
    // bit memory space is valid.
    OSMEMORYCONFORMITY_BITS32,
    // Same as 32 bits, just for 64 bits. On 64 bits systems this
    // translates to NONE, and on 32 bits systems this is not available
    // for use.
    OSMEMORYCONFORMITY_BITS64
};

typedef struct OSMemoryDescriptor {
    uuid_t       SHMTag;
    uintptr_t    StartAddress;
    uintptr_t    AllocationSize;
    unsigned int Attributes;
} OSMemoryDescriptor_t;

#endif //!__TYPES_MEMORY_H__
