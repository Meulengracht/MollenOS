/**
 * Copyright 2018, Philip Meulengracht
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
 * MollenOS System Component Infrastructure 
 * - The Memory component. This component has the task of managing
 *   different memory regions that map to physical components
 */

#ifndef __COMPONENT_MEMORY__
#define __COMPONENT_MEMORY__

#include <os/osdefs.h>
#include <irq_spinlock.h>
#include <utils/memory_stack.h>

#define MEMORY_MASK_COUNT 5

enum SystemMemoryAttributes {
    SystemMemoryAttributes_REMOVABLE,
    SystemMemoryAttributes_NONVOLATILE
};

typedef struct SystemMemoryRange {
    uintptr_t Start;
    size_t    Length;
} SystemMemoryRange_t;

typedef struct SystemMemoryMap {
    SystemMemoryRange_t Shared;
    SystemMemoryRange_t UserCode;
    SystemMemoryRange_t UserHeap;
    SystemMemoryRange_t ThreadLocal;
} SystemMemoryMap_t;

typedef struct SystemMemoryAllocatorRegion {
    MemoryStack_t Stack;
    IrqSpinlock_t Lock;
} SystemMemoryAllocatorRegion_t;

typedef struct SystemMemoryAllocator {
    int                           MaskCount;
    size_t                        Masks[MEMORY_MASK_COUNT];
    SystemMemoryAllocatorRegion_t Region[MEMORY_MASK_COUNT];
} SystemMemoryAllocator_t;

typedef struct SystemMemory {
    uintptr_t               PhysicalBase;
    size_t                  Size;
    size_t                  BlockSize;
    unsigned int            Attributes; // enum SystemMemoryAttributes
    SystemMemoryAllocator_t Allocator;
} SystemMemory_t;

/**
 * @brief
 *
 * @param systemMemory
 * @param physicalBase
 * @param size
 * @param blockSize
 * @param attributes
 */
KERNELAPI void KERNELABI
SystemMemoryConstruct(
        _In_ SystemMemory_t* systemMemory,
        _In_ uintptr_t       physicalBase,
        _In_ size_t          size,
        _In_ size_t          blockSize,
        _In_ unsigned int    attributes);

/**
 * @brief
 *
 * @param systemMemory
 * @param memoryMask
 * @param pageCount
 * @param pages
 * @return
 */
KERNELAPI oserr_t KERNELABI
SystemMemoryAllocate(
        _In_ SystemMemory_t* systemMemory,
        _In_ size_t          memoryMask,
        _In_ int             pageCount,
        _In_ uintptr_t*      pages);

/**
 * @brief
 *
 * @param systemMemory
 * @param pageCount
 * @param pages
 * @return
 */
KERNELAPI oserr_t KERNELABI
SystemMemoryFree(
        _In_ SystemMemory_t* systemMemory,
        _In_ int             pageCount,
        _In_ uintptr_t*      pages);

/**
 * @brief
 *
 * @param systemMemory
 * @param address
 * @return
 */
KERNELAPI oserr_t KERNELABI
SystemMemoryContainsAddress(
        _In_ SystemMemory_t* systemMemory,
        _In_ uintptr_t       address);

#endif // !__COMPONENT_MEMORY__
