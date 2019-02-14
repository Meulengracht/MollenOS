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

#include <os/mollenos.h>
#include <ddk/buffer.h>
#include <ddk/memory.h>

#include <modules/manager.h>
#include <memorybuffer.h>
#include <memoryspace.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <debug.h>

OsStatus_t
ScMemoryAllocate(
    _In_  size_t        Size, 
    _In_  Flags_t       Flags, 
    _Out_ uintptr_t*    VirtualAddress,
    _Out_ uintptr_t*    PhysicalAddress)
{
    uintptr_t            AllocatedAddress;
    SystemMemorySpace_t* Space = GetCurrentMemorySpace();
    
    assert(Space->Context != NULL);
    if (Size == 0) {
        return OsInvalidParameters;
    }
    
    // Now do the allocation in the user-bitmap 
    // since memory is managed in userspace for speed
    AllocatedAddress = AllocateBlocksInBlockmap(Space->Context->HeapSpace, __MASK, Size);
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
        if (CreateMemorySpaceMapping(Space, PhysicalAddress, &AllocatedAddress, Size, 
            ExtendedFlags, __MASK) != OsSuccess) {
            ReleaseBlockmapRegion(Space->Context->HeapSpace, AllocatedAddress, Size);
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

OsStatus_t 
ScMemoryFree(
    _In_ uintptr_t  Address, 
    _In_ size_t     Size)
{
    SystemMemorySpace_t* Space = GetCurrentMemorySpace();
    
    assert(Space->Context != NULL);
    if (Address == 0 || Size == 0) {
        return OsInvalidParameters;
    }

    // Now do the deallocation in the user-bitmap 
    // since memory is managed in userspace for speed
    TRACE("MemoryFree(V 0x%x, L 0x%x)", Address, Size);
    if (ReleaseBlockmapRegion(Space->Context->HeapSpace, Address, Size) != OsSuccess) {
        ERROR("ScMemoryFree(Address 0x%x, Size 0x%x) was invalid", Address, Size);
        return OsError;
    }
    return RemoveMemorySpaceMapping(Space, Address, Size);
}

OsStatus_t
ScMemoryProtect(
    _In_  void*    MemoryPointer,
    _In_  size_t   Length,
    _In_  Flags_t  Flags,
    _Out_ Flags_t* PreviousFlags)
{
    uintptr_t AddressStart = (uintptr_t)MemoryPointer;
    if (MemoryPointer == NULL || Length == 0) {
        return OsSuccess;
    }

    // We must force the application flag as it will remove
    // the user-accessibility if we allow it to change
    return ChangeMemorySpaceProtection(GetCurrentMemorySpace(), 
        AddressStart, Length, Flags | MAPPING_USERSPACE, PreviousFlags);
}

OsStatus_t
ScCreateBuffer(
    _In_  Flags_t       Flags,
    _In_  size_t        Size,
    _Out_ DmaBuffer_t*  MemoryBuffer)
{
    if (MemoryBuffer == NULL || Size == 0) {
        return OsError;
    }
    return CreateMemoryBuffer(Flags, Size, MemoryBuffer);
}

OsStatus_t
ScAcquireBuffer(
    _In_  UUId_t        Handle,
    _Out_ DmaBuffer_t*  MemoryBuffer)
{
    if (MemoryBuffer == NULL || Handle == UUID_INVALID) {
        return OsError;
    }
    return AcquireMemoryBuffer(Handle, MemoryBuffer);
}

OsStatus_t
ScQueryBuffer(
    _In_  UUId_t        Handle,
    _Out_ uintptr_t*    Dma,
    _Out_ size_t*       Capacity)
{
    if (Capacity == NULL || Dma == NULL || Handle == UUID_INVALID) {
        return OsError;
    }
    return QueryMemoryBuffer(Handle, Dma, Capacity);
}

OsStatus_t 
ScCreateMemorySpace(
    _In_  Flags_t Flags,
    _Out_ UUId_t* Handle)
{
    SystemModule_t* Module = GetCurrentModule();
    if (Handle == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }
    return CreateMemorySpace(Flags | MEMORY_SPACE_APPLICATION, Handle);
}

OsStatus_t 
ScGetThreadMemorySpaceHandle(
    _In_  UUId_t  ThreadHandle,
    _Out_ UUId_t* Handle)
{
    MCoreThread_t*  Thread;
    SystemModule_t* Module = GetCurrentModule();
    if (Handle == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }
    Thread = GetThread(ThreadHandle);
    if (Thread != NULL) {
        *Handle = Thread->MemorySpaceHandle;
        return OsSuccess;
    }
    return OsDoesNotExist;
}

OsStatus_t 
ScCreateMemorySpaceMapping(
    _In_ UUId_t                          Handle,
    _In_ struct MemoryMappingParameters* Parameters,
    _In_ DmaBuffer_t*                    AccessBuffer)
{
    SystemModule_t*      Module        = GetCurrentModule();
    SystemMemorySpace_t* MemorySpace   = (SystemMemorySpace_t*)LookupHandle(Handle);
    Flags_t              RequiredFlags = MAPPING_USERSPACE | MAPPING_PROVIDED | MAPPING_FIXED;
    OsStatus_t           Status;
    if (Parameters == NULL || AccessBuffer == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }
    if (MemorySpace == NULL || Parameters->Flags == 0) {
        return OsDoesNotExist;
    }
    
    Status = CreateMemoryBuffer(MEMORY_BUFFER_MEMORYMAPPING, Parameters->Length, AccessBuffer);
    if (Status != OsSuccess) {
        return Status;
    }

    if (Parameters->Flags | MEMORY_EXECUTABLE) {
        RequiredFlags |= MAPPING_EXECUTABLE;
    }
    if (!(Parameters->Flags & MEMORY_WRITE)) {
        RequiredFlags |= MAPPING_READONLY;
    }

    TRACE("CreateMemorySpaceMapping(P 0x%x, V 0x%x, L 0x%x, F 0x%x)", 
        AccessBuffer->Dma, Parameters->VirtualAddress, Parameters->Length, RequiredFlags);
    Status = CreateMemorySpaceMapping(MemorySpace, &AccessBuffer->Dma, &Parameters->VirtualAddress,
        Parameters->Length, RequiredFlags, __MASK);
    TRACE("=> mapped to 0x%x", AccessBuffer->Address);
    if (Status != OsSuccess) {
        ScMemoryFree(AccessBuffer->Address, AccessBuffer->Capacity);
        DestroyHandle(AccessBuffer->Handle);
        return Status;
    }
    return Status;
}
