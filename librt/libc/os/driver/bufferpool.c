/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - BufferPool Support Definitions & Structures
 * - This header describes the base bufferpool-structures, prototypes
 *   and functionality, refer to the individual things for descriptions
 */
//#define __TRACE

#include <os/bufferpool.h>
#include <os/osdefs.h>
#include <os/utils.h>
#include "../common/bytepool.h"
#include <stdlib.h>
#include <stddef.h>

typedef struct _BufferPool {
    DmaBuffer_t*    Buffer;
    BytePool_t*     Pool;
} BufferPool_t;

/* BufferPoolCreate
 * Creates a new buffer-pool from the given buffer object. 
 * This allows sub-allocations from a buffer-object. */
OsStatus_t
BufferPoolCreate(
    _In_  DmaBuffer_t*      Buffer,
    _Out_ BufferPool_t**    Pool)
{
    // Allocate the pool
    *Pool           = (BufferPool_t*)malloc(sizeof(BufferPool_t));
    (*Pool)->Buffer = Buffer;
    return bpool((void*)GetBufferDataPointer(Buffer), GetBufferSize(Buffer), &(*Pool)->Pool);
}

/* BufferPoolDestroy
 * Cleans up the buffer-pool and deallocates resources previously
 * allocated. This does not destroy the buffer-object. */
OsStatus_t
BufferPoolDestroy(
    _In_ BufferPool_t*      Pool)
{
    // Cleanup structure
    free(Pool->Pool);
    free(Pool);
    return OsSuccess;
}

/* BufferPoolAllocate
 * Allocates the requested size and outputs two addresses. The
 * virtual pointer to the accessible data, and the address of its 
 * corresponding physical address for hardware. */
OsStatus_t
BufferPoolAllocate(
    _In_  BufferPool_t*     Pool,
    _In_  size_t            Size,
    _Out_ uintptr_t**       VirtualPointer,
    _Out_ uintptr_t*        PhysicalAddress)
{
    // Variables
    void *Allocation = NULL;

    // Debug
    TRACE("BufferPoolAllocate(Size %u)", Size);

    // Perform an allocation
    Allocation = bget(Pool->Pool, Size);
    if (Allocation == NULL) {
        ERROR("Failed to allocate bufferpool memory (size %u)", Size);
        return OsError;
    }

    // Calculate the addresses and update out's
    *VirtualPointer     = (uintptr_t*)Allocation;
    *PhysicalAddress    = GetBufferDma(Pool->Buffer) 
        + ((uintptr_t)Allocation - (uintptr_t)GetBufferDataPointer(Pool->Buffer));
    TRACE(" > Virtual address 0x%x => Physical address 0x%x", (uintptr_t*)Allocation, *PhysicalAddress);
    return OsSuccess;
}

/* BufferPoolFree
 * Frees previously allocations made by the buffer-pool. The virtual
 * address must be the one passed back. */
OsStatus_t
BufferPoolFree(
    _In_ BufferPool_t*  Pool,
    _In_ uintptr_t*     VirtualPointer)
{
    brel(Pool->Pool, (void*)VirtualPointer);
    return OsSuccess;
}
