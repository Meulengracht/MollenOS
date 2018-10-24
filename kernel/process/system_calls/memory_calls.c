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
 * MollenOS MCore - System Calls
 */
#define __MODULE "SCIF"
//#define __TRACE

#include <os/osdefs.h>
#include <os/mollenos.h>
#include <os/buffer.h>
#include <process/phoenix.h>
#include <memoryspace.h>
#include <memorybuffer.h>
#include <machine.h>
#include <debug.h>

/* ScMemoryAllocate
 * Allows a process to allocate memory from the userheap, it takes a size and allocation flags */
OsStatus_t
ScMemoryAllocate(
    _In_  size_t        Size, 
    _In_  Flags_t       Flags, 
    _Out_ uintptr_t*    VirtualAddress,
    _Out_ uintptr_t*    PhysicalAddress)
{
    // Variables
    uintptr_t AllocatedAddress;
    MCoreAsh_t *Ash;

    // Locate the current running process
    Ash = GetCurrentProcess();
    if (Ash == NULL || Size == 0) {
        return OsError;
    }
    
    // Now do the allocation in the user-bitmap 
    // since memory is managed in userspace for speed
    AllocatedAddress = AllocateBlocksInBlockmap(Ash->Heap, __MASK, Size);
    if (AllocatedAddress == 0) {
        return OsError;
    }

    // Force a commit of memory if any flags
    // is given, because we can't apply flags later
    if (Flags != 0) {
        Flags |= MEMORY_COMMIT;
    }

    // Handle flags
    // If the commit flag is not given the flags won't be applied
    if (Flags & MEMORY_COMMIT) {
        int ExtendedFlags = MAPPING_USERSPACE | MAPPING_FIXED;

        // Build extensions
        if (Flags & MEMORY_CONTIGIOUS) {
            ExtendedFlags |= MAPPING_CONTIGIOUS;
        }
        if (Flags & MEMORY_UNCHACHEABLE) {
            ExtendedFlags |= MAPPING_NOCACHE;
        }
        if (Flags & MEMORY_LOWFIRST) {
            ExtendedFlags |= MAPPING_LOWFIRST;
        }

        // Do the actual mapping
        if (CreateSystemMemorySpaceMapping(GetCurrentSystemMemorySpace(), 
            PhysicalAddress, &AllocatedAddress, Size, ExtendedFlags, __MASK) != OsSuccess) {
            ReleaseBlockmapRegion(Ash->Heap, AllocatedAddress, Size);
            *VirtualAddress = 0;
            return OsError;
        }

        // Handle post allocation flags
        if (Flags & MEMORY_CLEAN) {
            memset((void*)AllocatedAddress, 0, Size);
        }
    }
    else {
        *PhysicalAddress = 0;
    }

    // Update out and return
    *VirtualAddress = (uintptr_t)AllocatedAddress;
    return OsSuccess;
}

/* ScMemoryFree
 * Free's previous allocated memory, given an address and a size (though not needed for now!) */
OsStatus_t 
ScMemoryFree(
    _In_ uintptr_t  Address, 
    _In_ size_t     Size)
{
    // Variables
    MCoreAsh_t *Ash = NULL;

    // Locate the current running process
    Ash = GetCurrentProcess();
    if (Ash == NULL || Address == 0 || Size == 0) {
        return OsError;
    }

    // Now do the deallocation in the user-bitmap 
    // since memory is managed in userspace for speed
    if (ReleaseBlockmapRegion(Ash->Heap, Address, Size) != OsSuccess) {
        ERROR("ScMemoryFree(Address 0x%x, Size 0x%x) was invalid", Address, Size);
        return OsError;
    }
    return RemoveSystemMemoryMapping(GetCurrentSystemMemorySpace(), Address, Size);
}

/* ScMemoryQuery
 * Queries information about a chunk of memory 
 * and returns allocation information or stats depending on query function */
OsStatus_t
ScMemoryQuery(
    _Out_ MemoryDescriptor_t *Descriptor)
{
    Descriptor->AllocationGranularityBytes = GetMachine()->MemoryGranularity;
    Descriptor->PageSizeBytes   = GetSystemMemoryPageSize();
    Descriptor->PagesTotal      = GetMachine()->PhysicalMemory.BlockCount;
    Descriptor->PagesUsed       = GetMachine()->PhysicalMemory.BlocksAllocated;
    return OsSuccess;
}

/* MemoryProtect
 * Changes the protection flags of a previous memory allocation
 * made by MemoryAllocate */
OsStatus_t
ScMemoryProtect(
    _In_  void*     MemoryPointer,
    _In_  size_t    Length,
    _In_  Flags_t   Flags,
    _Out_ Flags_t*  PreviousFlags)
{
    // Variables
    uintptr_t AddressStart = (uintptr_t)MemoryPointer;
    if (MemoryPointer == NULL || Length == 0) {
        return OsSuccess;
    }

    // We must force the application flag as it will remove
    // the user-accessibility if we allow it to change
    return ChangeSystemMemorySpaceProtection(GetCurrentSystemMemorySpace(), 
        AddressStart, Length, Flags | MAPPING_USERSPACE, PreviousFlags);
}

/* ScCreateBuffer
 * Creates a new system memory buffer and fills in the details for the 
 * given buffer-object */
OsStatus_t
ScCreateBuffer(
    _In_  Flags_t       Flags,
    _In_  size_t        Size,
    _Out_ DmaBuffer_t*  MemoryBuffer)
{
    // Variables
    if (MemoryBuffer == NULL || Size == 0) {
        return OsError;
    }
    return CreateMemoryBuffer(Flags, Size, MemoryBuffer);
}

/* ScAcquireBuffer
 * Acquires an existing memory buffer handle to the current memory space.
 * This provides access to the memory the handle is associated with. */
OsStatus_t
ScAcquireBuffer(
    _In_  UUId_t        Handle,
    _Out_ DmaBuffer_t*  MemoryBuffer)
{
    // Variables
    if (MemoryBuffer == NULL || Handle == UUID_INVALID) {
        return OsError;
    }
    return AcquireMemoryBuffer(Handle, MemoryBuffer);
}

/* ScQueryBuffer
 * Queries an existing memory buffer handle. Returns the metrics for the
 * system buffer. */
OsStatus_t
ScQueryBuffer(
    _In_  UUId_t        Handle,
    _Out_ uintptr_t*    Dma,
    _Out_ size_t*       Capacity)
{
    // Variables
    if (Capacity == NULL || Dma == NULL || Handle == UUID_INVALID) {
        return OsError;
    }
    return QueryMemoryBuffer(Handle, Dma, Capacity);
}
