/* MollenOS
 *
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Memory Buffer Interface
 * - Implementation of the memory dma buffers. This provides a transfer buffer
 *   that is not bound to any specific virtual memory area but instead are bound
 *   to fixed physical addreses.
 */
#define __MODULE "MBUF"
//#define __TRACE

#include <memorybuffer.h>
#include <machine.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

OsStatus_t
CreateMemoryBuffer(
    _In_  uintptr_t* DmaVector,
    _In_  int        EntryCount,
    _Out_ UUId_t*    HandleOut)
{
    BlockVector_t* Vector = (BlockVector_t*)kmalloc(
        sizeof(BlockVector_t) + EntryCount * sizeof(uintptr_t));
    if (!Vector) {
        return OsOutOfMemory;
    }
    
    Vector->BlockCount = EntryCount;
    memcpy(&Vector->Blocks[0], &DmaVector[0], EntryCount * sizeof(uintptr_t));
    return CreateHandle(HandleTypeMemoryBuffer, Vector);
}

OsStatus_t
DestroyMemoryBuffer(
    _In_ void* Resource)
{
    BlockVector_t* Vector = (BlockVector_t*)Resource;
    OsStatus_t     Status;
    for (int i = 0; i < Vector->BlockCount; i++) {
        Status = FreeSystemMemory(Vector->Blocks[i], GetMachine()->MemoryGranularity);
    }
    kfree(Vector);
    return Status;
}
