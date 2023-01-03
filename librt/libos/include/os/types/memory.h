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
#define MEMORY_LOWFIRST      0x00000002U                  // Allocate from low memory
#define MEMORY_CLEAN         0x00000004U                  // Memory should be cleaned
#define MEMORY_UNCHACHEABLE  0x00000008U                  // Memory must not be cached
#define MEMORY_CLONE         0x00000010U                  // Clone the memory mapping passed in as hint
#define MEMORY_FIXED         0x00000020U                  // Use the value provided in Hint

#define MEMORY_READ          0x00000100U                  // Memory is readable
#define MEMORY_WRITE         0x00000200U                  // Memory is writable
#define MEMORY_EXECUTABLE    0x00000400U                  // Memory is executable
#define MEMORY_DIRTY         0x00000800U                  // Memory is dirty

typedef struct OSMemoryDescriptor {
    uintptr_t    StartAddress;
    uintptr_t    AllocationSize;
    unsigned int Attributes;
} OSMemoryDescriptor_t;

#endif //!__TYPES_MEMORY_H__
