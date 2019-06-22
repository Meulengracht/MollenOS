/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * BufferPool Support Definitions & Structures
 * - This header describes the base bufferpool-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
//#define __TRACE

#include <ddk/bufferpool.h>
#include <ddk/memory.h>
#include <ddk/utils.h>
#include "../common/bytepool.h"
#include <stdlib.h>

typedef struct _BufferPool {
    UUId_t      BufferHandle;
    BytePool_t* Pool;
    uintptr_t   VirtualBase;
    uintptr_t   DmaVector[1];
} BufferPool_t;

OsStatus_t
BufferPoolCreate(
    _In_  UUId_t         BufferHandle,
    _In_  void*          Buffer,
    _Out_ BufferPool_t** PoolOut)
{
    BufferPool_t* Pool;
    size_t        Length;
    OsStatus_t    Status;
    int           VectorSize;
    
    // Get buffer metrics and mappings
    Status = MemoryGetSharedMetrics(BufferHandle, &Length, NULL);
    if (Status != OsSuccess) {
        return Status;
    }
    
    // @todo get the page size from system
    VectorSize = Length / 0x1000;
    
    // Allocate the pool
    Pool               = (BufferPool_t*)malloc(sizeof(BufferPool_t) + (VectorSize * sizeof(uintptr_t)));
    Pool->BufferHandle = BufferHandle;
    Pool->VirtualBase  = (uintptr_t)Buffer;
    Status             = MemoryGetSharedMetrics(BufferHandle, NULL, &Pool->DmaVector[0]);
    Status             = bpool(Buffer, Length, &Pool->Pool);
    return Status;
}

OsStatus_t
BufferPoolDestroy(
    _In_ BufferPool_t* Pool)
{
    free(Pool->Pool);
    free(Pool);
    return OsSuccess;
}

OsStatus_t
BufferPoolAllocate(
    _In_  BufferPool_t* Pool,
    _In_  size_t        Size,
    _Out_ uintptr_t**   VirtualPointer,
    _Out_ uintptr_t*    PhysicalAddress)
{
    ptrdiff_t Difference;
    void*     Allocation;

    TRACE("BufferPoolAllocate(Size %u)", Size);

    Allocation = bget(Pool->Pool, Size);
    if (!Allocation) {
        ERROR("Failed to allocate bufferpool memory (size %u)", Size);
        return OsError;
    }
    
    // Calculate the addresses and update out's
    Difference       = (uintptr_t)Allocation - Pool->VirtualBase;
    *PhysicalAddress = Pool->DmaVector[Difference / 0x1000] + (Difference % 0x1000);
    *VirtualPointer  = (uintptr_t*)Allocation;
    TRACE(" > Virtual address 0x%x => Physical address 0x%x", (uintptr_t*)Allocation, *PhysicalAddress);
    return OsSuccess;
}

OsStatus_t
BufferPoolFree(
    _In_ BufferPool_t* Pool,
    _In_ uintptr_t*    VirtualPointer)
{
    brel(Pool->Pool, (void*)VirtualPointer);
    return OsSuccess;
}
