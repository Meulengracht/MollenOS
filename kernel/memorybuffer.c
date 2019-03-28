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
 * MollenOS Memory Buffer Interface
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
    _In_  size_t        Size,
    _Out_ DmaBuffer_t*  MemoryBuffer)
{
    SystemMemoryBuffer_t* SystemBuffer;
    SystemMemorySpace_t*  Space      = GetCurrentMemorySpace();
    OsStatus_t            Status     = OsSuccess;
    uintptr_t             DmaAddress = 0;
    uintptr_t             Virtual    = 0;
    size_t                Capacity;
    UUId_t                Handle;

    Capacity = DIVUP(Size, GetMemorySpacePageSize()) * GetMemorySpacePageSize();
    Status   = CreateMemorySpaceMapping(Space, &DmaAddress, 
        &Virtual, Capacity, MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT,
        MAPPING_PHYSICAL_CONTIGIOUS | MAPPING_VIRTUAL_PROCESS, __MASK);
    if (Status != OsSuccess) {
        ERROR("Failed to map system memory");
        return Status;
    }

    SystemBuffer            = (SystemMemoryBuffer_t*)kmalloc(sizeof(SystemMemoryBuffer_t));
    Handle                  = CreateHandle(HandleTypeMemoryBuffer, 0, SystemBuffer);
    SystemBuffer->Capacity  = Capacity;
    SystemBuffer->Physical  = DmaAddress;

    // Update the user-provided structure
    if (MemoryBuffer != NULL) {
        MemoryBuffer->Handle   = Handle;
        MemoryBuffer->Dma      = DmaAddress;
        MemoryBuffer->Capacity = Capacity;
        MemoryBuffer->Address  = Virtual;
    }
    return Status;
}

OsStatus_t
AcquireMemoryBuffer(
    _In_  UUId_t       Handle,
    _Out_ DmaBuffer_t* MemoryBuffer)
{
    SystemMemoryBuffer_t* SystemBuffer;
    OsStatus_t            Status;
    uintptr_t             Virtual;

    // We acquire the buffer by mapping it into our address space
    // and adding a reference
    SystemBuffer = AcquireHandle(Handle);
    if (SystemBuffer == NULL) {
        ERROR("Invalid memory buffer handle 0x%" PRIxIN "", Handle);
        return OsError;
    }

    // Map it in to make sure we can do it
    Status = CreateMemorySpaceMapping(GetCurrentMemorySpace(), &SystemBuffer->Physical, 
        &Virtual, SystemBuffer->Capacity, MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT,
        MAPPING_PHYSICAL_FIXED | MAPPING_VIRTUAL_PROCESS, __MASK);
    if (Status != OsSuccess) {
        ERROR("Failed to map process memory");
        return Status;
    }

    // Update the user-provided structure
    MemoryBuffer->Handle    = Handle;
    MemoryBuffer->Dma       = SystemBuffer->Physical;
    MemoryBuffer->Capacity  = SystemBuffer->Capacity;
    MemoryBuffer->Address   = Virtual;
    return Status;
}

OsStatus_t
QueryMemoryBuffer(
    _In_  UUId_t        Handle,
    _Out_ uintptr_t*    Dma,
    _Out_ size_t*       Capacity)
{
    SystemMemoryBuffer_t *SystemBuffer;

    // We acquire the buffer by mapping it into our address space
    // and adding a reference
    SystemBuffer = LookupHandle(Handle);
    if (SystemBuffer == NULL) {
        return OsError;
    }
    *Dma      = SystemBuffer->Physical;
    *Capacity = SystemBuffer->Capacity;
    return OsSuccess;
}

OsStatus_t
DestroyMemoryBuffer(
    _In_ void* Resource)
{
    SystemMemoryBuffer_t* SystemBuffer = (SystemMemoryBuffer_t*)Resource;
    OsStatus_t            Status       = OsSuccess;
    
    Status = FreeSystemMemory(SystemBuffer->Physical, SystemBuffer->Capacity);
    kfree(Resource);
    return Status;
}
