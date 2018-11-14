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

#include <process/process.h>
#include <memorybuffer.h>
#include <machine.h>
#include <handle.h>
#include <debug.h>
#include <heap.h>

/* CreateMemoryBuffer 
 * Creates a new memory buffer instance of the given size. The allocation
 * of resources happens at this call, and reference is set to 1. Size is automatically
 * rounded up to a block-alignment */
OsStatus_t
CreateMemoryBuffer(
    _In_  Flags_t       Flags,
    _In_  size_t        Size,
    _Out_ DmaBuffer_t*  MemoryBuffer)
{
    SystemMemoryBuffer_t *SystemBuffer;
    OsStatus_t Status       = OsSuccess;
    uintptr_t DmaAddress    = 0;
    uintptr_t Virtual       = 0;
    size_t Capacity;
    UUId_t Handle;

    Capacity = DIVUP(Size, GetSystemMemoryPageSize()) * GetSystemMemoryPageSize();
    switch (MEMORY_BUFFER_TYPE(Flags)) {
        case MEMORY_BUFFER_KERNEL: {
            DmaAddress = AllocateSystemMemory(Capacity, __MASK, MEMORY_DOMAIN);
            if (DmaAddress == 0) {
                ERROR("Failed to allocate system memory");
                return OsError;
            }

            Status = CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), &DmaAddress, 
                &Virtual, Capacity, MAPPING_PROVIDED | MAPPING_PERSISTENT | MAPPING_KERNEL, __MASK);
            if (Status != OsSuccess) {
                ERROR("Failed to map system memory");
                FreeSystemMemory(DmaAddress, Capacity);
                return Status;
            }
        } break;

        case MEMORY_BUFFER_DEFAULT: {
            DmaAddress = AllocateSystemMemory(Capacity, __MASK, MEMORY_DOMAIN);
            if (DmaAddress == 0) {
                ERROR("Failed to allocate system memory");
                return OsError;
            }

            Status = CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), &DmaAddress, 
                &Virtual, Capacity, MAPPING_USERSPACE | MAPPING_PROVIDED | MAPPING_PERSISTENT | MAPPING_PROCESS, __MASK);
            if (Status != OsSuccess) {
                ERROR("Failed to map system memory");
                FreeSystemMemory(DmaAddress, Capacity);
                return Status;
            }
        } break;

        case MEMORY_BUFFER_FILEMAPPING: {
            SystemProcess_t* CurrentProcess = GetCurrentProcess();
            assert(CurrentProcess != NULL);
            Virtual = AllocateBlocksInBlockmap(CurrentProcess->Heap, __MASK, Size);
            if (Virtual == 0) {
                ERROR("Failed to allocate heap memory");
                return OsError;
            }
        } break;

        default: {
            ERROR("Unknown memory buffer option");
            return OsError;
        } break;
    }

    SystemBuffer            = (SystemMemoryBuffer_t*)kmalloc(sizeof(SystemMemoryBuffer_t));
    Handle                  = CreateHandle(HandleTypeMemoryBuffer, 0, SystemBuffer);
    SystemBuffer->Flags     = Flags;
    SystemBuffer->Capacity  = Capacity;
    SystemBuffer->Physical  = DmaAddress;

    // Update the user-provided structure
    if (MemoryBuffer != NULL) {
        MemoryBuffer->Handle    = Handle;
        MemoryBuffer->Dma       = DmaAddress;
        MemoryBuffer->Capacity  = Capacity;
        MemoryBuffer->Address   = Virtual;
    }
    return Status;
}

/* AcquireMemoryBuffer
 * Acquires an existing memory buffer into the current memory space. This will
 * add it to the list of in-use buffers and increase reference count. */
OsStatus_t
AcquireMemoryBuffer(
    _In_  UUId_t        Handle,
    _Out_ DmaBuffer_t*  MemoryBuffer)
{
    SystemMemoryBuffer_t *SystemBuffer;
    OsStatus_t Status;
    uintptr_t Virtual;

    // We acquire the buffer by mapping it into our address space
    // and adding a reference
    SystemBuffer = AcquireHandle(Handle);
    if (SystemBuffer == NULL) {
        ERROR("Invalid memory buffer handle 0x%x", Handle);
        return OsError;
    }

    // Map it in to make sure we can do it
    Status = CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), &SystemBuffer->Physical, 
        &Virtual, SystemBuffer->Capacity, MAPPING_USERSPACE | 
        MAPPING_PROVIDED | MAPPING_PERSISTENT | MAPPING_PROCESS, __MASK);
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

/* QueryMemoryBuffer
 * Queries the handle for information instead of acquiring the memory buffer. This
 * can be usefull when no access is needed to the buffer. */
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

    // Update outs
    *Dma        = SystemBuffer->Physical;
    *Capacity   = SystemBuffer->Capacity;
    return OsSuccess;
}

/* DestroyMemoryBuffer
 * Cleans up the resources associated with the handle. This function is registered
 * with the handle manager. */
OsStatus_t
DestroyMemoryBuffer(
    _In_  void*         Resource)
{
    SystemMemoryBuffer_t *SystemBuffer;
    OsStatus_t Status = OsSuccess;

    // Free the physical pages associated, then cleanup structure
    // Only free in the case it's not empty
    SystemBuffer = (SystemMemoryBuffer_t*)Resource;
    switch (MEMORY_BUFFER_TYPE(SystemBuffer->Flags)) {
        case MEMORY_BUFFER_DEFAULT:
        case MEMORY_BUFFER_KERNEL: {
            Status = FreeSystemMemory(SystemBuffer->Physical, SystemBuffer->Capacity);
        } break;

        default: {

        } break;
    }

    kfree(Resource);
    return Status;
}
