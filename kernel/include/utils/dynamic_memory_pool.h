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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Datastructure (Dynamic Memory Pool)
 * - Implementation of a memory pool as a binary-tree.
 */

#ifndef __UTILS_DYNAMIC_MEMORY_POOL_H__
#define __UTILS_DYNAMIC_MEMORY_POOL_H__

#include <os/osdefs.h>
#include <irq_spinlock.h>

PACKED_TYPESTRUCT(DynamicMemoryChunk, {
	struct DynamicMemoryChunk* Parent;
	struct DynamicMemoryChunk* Left;
	struct DynamicMemoryChunk* Right;
	uint8_t                    Split : 1;
	uint8_t                    Allocated : 1;
	uint8_t                    Reserved : 6;
});

typedef struct DynamicMemoryPool {
	uintptr_t             StartAddress;
	size_t                Length;
	size_t                ChunkSize;
	DynamicMemoryChunk_t* Root;
	IrqSpinlock_t         SyncObject;
} DynamicMemoryPool_t;

KERNELAPI void KERNELABI
DynamicMemoryPoolConstruct(
    _In_ DynamicMemoryPool_t* Pool,
    _In_ uintptr_t            StartAddress,
    _In_ size_t               Length,
    _In_ size_t               ChunkSize);

KERNELAPI void KERNELABI
DynamicMemoryPoolDestroy(
    _In_ DynamicMemoryPool_t* Pool);

KERNELAPI uintptr_t KERNELABI
DynamicMemoryPoolAllocate(
    _In_ DynamicMemoryPool_t* Pool,
    _In_ size_t               Length);

KERNELAPI void KERNELABI
DynamicMemoryPoolFree(
    _In_ DynamicMemoryPool_t* Pool,
    _In_ uintptr_t            Address);

// Returns 1 if contains
KERNELAPI int KERNELABI
DynamicMemoryPoolContains(
    _In_ DynamicMemoryPool_t* Pool,
    _In_ uintptr_t            Address);

#endif //!__UTILS_DYNAMIC_MEMORY_POOL_H__
