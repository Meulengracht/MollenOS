/**
 * Copyright 2021, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 * MollenOS System Component Infrastructure
 * - The Memory component. This component has the task of managing
 *   different memory regions that map to physical components
 */

#ifndef __UTILS_MEMORYSTACK_H__
#define __UTILS_MEMORYSTACK_H__

#include <os/osdefs.h>

struct MemoryStackItem;

typedef struct {
    int                     capacity;
    int                     index;
    size_t                  block_size;
    size_t                  data_size;
    struct MemoryStackItem* items;
} MemoryStack_t;

/**
 * @brief
 *
 * @param stack
 * @param blockSize
 * @param dataAddress
 * @param dataSize
 */
KERNELAPI void KERNELABI
MemoryStackConstruct(
        _In_ MemoryStack_t* stack,
        _In_ size_t         blockSize,
        _In_ uintptr_t      dataAddress,
        _In_ size_t         dataSize);

/**
 * @brief We simply just update the address pointer. This function is only here
 * because the OS needs to relocate the stack to a virtual address instead
 * of having this stack identity mapped.
 *
 * @param stack
 * @param address
 */
KERNELAPI void KERNELABI
MemoryStackRelocate(
        _In_ MemoryStack_t* stack,
        _In_ uintptr_t      address);

/**
 * @brief
 *
 * @param stack
 * @param address
 * @param blockCount
 */
KERNELAPI void KERNELABI
MemoryStackPush(
        _In_ MemoryStack_t* stack,
        _In_ uintptr_t      address,
        _In_ int            blockCount);


/**
 * @brief
 *
 * @param stack
 * @param blocks
 * @param blockCount
 */
KERNELAPI void KERNELABI
MemoryStackPushMultiple(
        _In_ MemoryStack_t*   stack,
        _In_ const uintptr_t* blocks,
        _In_ int              blockCount);

/**
 * @brief
 *
 * @param stack
 * @param blockCount
 * @param blocks
 * @return
 */
KERNELAPI OsStatus_t KERNELABI
MemoryStackPop(
        _In_ MemoryStack_t* stack,
        _In_ int*           blockCount,
        _In_ uintptr_t*     blocks);

#endif //!__UTILS_MEMORYSTACK_H__
