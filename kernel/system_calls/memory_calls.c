/**
 * MollenOS
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
 * Memory related system call implementations
 */

#define __MODULE "SCIF"
//#define __TRACE

#include <ddk/memory.h>

#include <os/mollenos.h>
#include <os/dmabuf.h>

#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <modules/manager.h>
#include <memoryspace.h>
#include <threading.h>
#include <machine.h>
#include <string.h>

OsStatus_t
ScMemoryAllocate(
    _In_      void*   Hint,
    _In_      size_t  Length,
    _In_      Flags_t Flags,
    _Out_     void**  MemoryOut)
{
    OsStatus_t           Status;
    uintptr_t            AllocatedAddress;
    SystemMemorySpace_t* Space          = GetCurrentMemorySpace();
    Flags_t              MemoryFlags    = MAPPING_USERSPACE;
    Flags_t              PlacementFlags = MAPPING_VIRTUAL_PROCESS;
    int                  PageCount;
    uintptr_t*           Pages; 
    
    if (!Length || !MemoryOut) {
        return OsInvalidParameters;
    }

    PageCount = DIVUP(Length, GetMemorySpacePageSize());
    Pages     = kmalloc(sizeof(uintptr_t) * PageCount);
    if (!Pages) {
        return OsOutOfMemory;
    }

    // Convert flags from memory domain to memory space domain
    if (Flags & MEMORY_COMMIT) {
        MemoryFlags |= MAPPING_COMMIT;
    }
    else {
        // Zero page mappings to mark them reserved
        memset(Pages, 0, sizeof(uintptr_t) * PageCount);
    }
    
    if (Flags & MEMORY_UNCHACHEABLE) {
        MemoryFlags |= MAPPING_NOCACHE;
    }
    if (Flags & MEMORY_LOWFIRST) {
        MemoryFlags |= MAPPING_LOWFIRST;
    }
    if (!(Flags & MEMORY_WRITE)) {
        //MemoryFlags |= MAPPING_READONLY;
    }
    if (Flags & MEMORY_EXECUTABLE) {
        MemoryFlags |= MAPPING_EXECUTABLE;
    }
    
    // Create the actual mappings
    Status = MemorySpaceMap(Space, &AllocatedAddress, Pages, Length, 
        MemoryFlags, PlacementFlags);
    if (Status == OsSuccess) {
        *MemoryOut = (void*)AllocatedAddress;
        
        if ((Flags & (MEMORY_COMMIT | MEMORY_CLEAN)) == (MEMORY_COMMIT | MEMORY_CLEAN)) {
            memset((void*)AllocatedAddress, 0, Length);
        }
    }
    kfree(Pages);
    return Status;
}

OsStatus_t 
ScMemoryFree(
    _In_ uintptr_t  Address, 
    _In_ size_t     Size)
{
    SystemMemorySpace_t* Space = GetCurrentMemorySpace();
    if (Address == 0 || Size == 0) {
        return OsInvalidParameters;
    }
    return MemorySpaceUnmap(Space, Address, Size);
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
    return ChangeMemorySpaceProtection(GetCurrentMemorySpace(), 
        AddressStart, Length, Flags | MAPPING_USERSPACE, PreviousFlags);
}

OsStatus_t
ScDmaCreate(
    _In_ struct dma_buffer_info* info,
    _In_ struct dma_attachment*  attachment)
{
    OsStatus_t Status;
    Flags_t    Flags = 0;

    if (!info || !attachment) {
        return OsInvalidParameters;
    }
    
    TRACE("ScDmaCreate(%u, 0x%x)", LODWORD(info->length), info->flags);

    if (info->flags & DMA_PERSISTANT) {
        Flags |= MAPPING_PERSISTENT;
    }
    
    if (info->flags & DMA_UNCACHEABLE) {
        Flags |= MAPPING_NOCACHE;
    }
    
    Status = MemoryCreateSharedRegion(info->length, info->capacity, 
        Flags, &attachment->buffer, &attachment->handle);
    if (Status != OsSuccess) {
        return Status;
    }
    
    if (info->flags & DMA_CLEAN) {
        memset(attachment->buffer, 0, info->length);
    }

    attachment->length = info->length;
    return Status;
}

OsStatus_t
ScDmaExport(
    _In_ void*                   buffer,
    _In_ struct dma_buffer_info* info,
    _In_ struct dma_attachment*  attachment)
{
    OsStatus_t Status;
    Flags_t    Flags = 0;

    if (!buffer || !info || !attachment) {
        return OsInvalidParameters;
    }
    
    if (info->flags & DMA_PERSISTANT) {
        Flags |= MAPPING_PERSISTENT;
    }
    
    if (info->flags & DMA_UNCACHEABLE) {
        Flags |= MAPPING_NOCACHE;
    }
    
    TRACE("ScDmaExport(0x%" PRIxIN ", %u)", buffer, LODWORD(info->length));
    
    Status = MemoryExportSharedRegion(buffer, info->length,
        Flags, &attachment->handle);
    if (Status != OsSuccess) {
        return Status;
    }

    if (info->flags & DMA_CLEAN) {
        memset(buffer, 0, info->length);
    }
    
    attachment->buffer = buffer;
    attachment->length = info->length;
    return Status;
}

OsStatus_t
ScDmaAttach(
    _In_ UUId_t                 handle,
    _In_ struct dma_attachment* attachment)
{
    SystemSharedRegion_t* Region;
    
    if (!attachment) {
        ERROR("[sc_dma_attach] null attachment pointer");
        return OsInvalidParameters;
    }
    
    Region = (SystemSharedRegion_t*)AcquireHandle(handle);
    if (!Region) {
        ERROR("[sc_dma_attach] [acquire_handle] invalid handle %u", handle);
        return OsDoesNotExist;
    }
    
    // Update the attachment with info as it were correct
    attachment->handle = handle;
    attachment->length = Region->Length;
    attachment->buffer = NULL;
    return OsSuccess;
}

OsStatus_t
ScDmaAttachmentMap(
    _In_ struct dma_attachment* attachment)
{
    SystemSharedRegion_t* Region;
    OsStatus_t            Status;
    uintptr_t             Offset;
    uintptr_t             Address;
    size_t                Length;
    
    if (!attachment || (attachment->buffer != NULL)) {
        return OsInvalidParameters;
    }
    TRACE("ScDmaAttachmentMap(0x%x)", LODWORD(attachment->handle));
    
    Region = (SystemSharedRegion_t*)LookupHandleOfType(
        attachment->handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    MutexLock(&Region->SyncObject);
    
    // This is more tricky, for the calling process we must make a new
    // mapping that spans the entire Capacity, but is uncommitted, and then commit
    // the Length of it.
    Length = MIN(attachment->length, Region->Length);
    Offset = (Region->Pages[0] % GetMemorySpacePageSize());
    
    TRACE("... create vmem mappings of length 0x%x", LODWORD(Region->Capacity + Offset));
    Status = MemorySpaceMapReserved(GetCurrentMemorySpace(),
        (VirtualAddress_t*)&Address, Region->Capacity + Offset, 
        MAPPING_USERSPACE | MAPPING_PERSISTENT, MAPPING_VIRTUAL_PROCESS);
    if (Status != OsSuccess) {
        return Status;
    }
    
    // Now commit <Length> in pages
    TRACE("... committing vmem mappings of length 0x%x", LODWORD(Length));
    Status = MemorySpaceCommit(GetCurrentMemorySpace(),
        Address, &Region->Pages[0], Length, MAPPING_PHYSICAL_FIXED);
    MutexUnlock(&Region->SyncObject);
    
    attachment->buffer = (void*)(Address + Offset);
    attachment->length = Length;
    return Status;
}

OsStatus_t
ScDmaAttachmentResize(
    _In_ struct dma_attachment* attachment,
    _In_ size_t                 length)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return MemoryResizeSharedRegion(attachment->handle, attachment->buffer, length);
}

OsStatus_t
ScDmaAttachmentRefresh(
    _In_ struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return MemoryRefreshSharedRegion(attachment->handle, attachment->buffer, 
        attachment->length, &attachment->length);
}

OsStatus_t
ScDmaAttachmentUnmap(
    _In_ struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return ScMemoryFree((uintptr_t)attachment->buffer, attachment->length);
}

OsStatus_t
ScDmaDetach(
    _In_ struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    DestroyHandle(attachment->handle);
    return OsSuccess;
}

OsStatus_t
ScDmaGetMetrics(
    _In_  struct dma_attachment* attachment,
    _Out_ int*                   sg_count_out,
    _Out_ struct dma_sg*         sg_list_out)
{
    SystemSharedRegion_t* Region;
    size_t                PageSize = GetMemorySpacePageSize();
    
    if (!attachment || !sg_count_out) {
        return OsInvalidParameters;
    }
    
    Region = (SystemSharedRegion_t*)LookupHandleOfType(
        attachment->handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    // Requested count of the scatter-gather units, so count
    // how many entries it would take to fill a list
    // Assume that if both pointers are supplied we are trying to fill
    // the list with the requested amount, and thus skip this step.
    if (sg_count_out && !sg_list_out) {
        int sg_count = 0;
        for (int i = 0; i < Region->PageCount; i++) {
            if (i == 0 || (Region->Pages[i - 1] + PageSize) != Region->Pages[i]) {
                sg_count++;
            }
        }
        *sg_count_out = sg_count;
    }
    
    // In order to get the list both counters must be filled
    if (sg_count_out && sg_list_out) {
        int sg_count = *sg_count_out;
        for (int i = 0, j = 0; (i < sg_count) && (j < Region->PageCount); i++) {
            struct dma_sg* sg = &sg_list_out[i];
            
            sg->address = Region->Pages[j++];
            sg->length  = PageSize;
            
            while ((j < Region->PageCount) &&
                   (Region->Pages[j - 1] + PageSize) == Region->Pages[j]) {
                sg->length += PageSize;
                j++;
            }
        }
        
        // Adjust the initial sg entry for offset
        sg_list_out[0].length -= sg_list_out[0].address % PageSize;
    }
    return OsSuccess;
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
    Thread = (MCoreThread_t*)LookupHandleOfType(ThreadHandle, HandleTypeThread);
    if (Thread != NULL) {
        *Handle = Thread->MemorySpaceHandle;
        return OsSuccess;
    }
    return OsDoesNotExist;
}

OsStatus_t 
ScCreateMemorySpaceMapping(
    _In_  UUId_t                          Handle,
    _In_  struct MemoryMappingParameters* Parameters,
    _Out_ void**                          AddressOut)
{
    SystemModule_t*      Module         = GetCurrentModule();
    SystemMemorySpace_t* MemorySpace    = (SystemMemorySpace_t*)LookupHandleOfType(Handle, HandleTypeMemorySpace);
    Flags_t              RequiredFlags  = MAPPING_COMMIT | MAPPING_USERSPACE;
    Flags_t              PlacementFlags = MAPPING_VIRTUAL_FIXED;
    VirtualAddress_t     CopyPlacement  = 0;
    int                  PageCount;
    uintptr_t*           Pages;
    OsStatus_t           Status;
    
    if (Parameters == NULL || AddressOut == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsInvalidParameters;
    }
    
    if (MemorySpace == NULL) {
        return OsDoesNotExist;
    }
    
    PageCount = DIVUP(Parameters->Length, GetMemorySpacePageSize());
    Pages     = kmalloc(sizeof(uintptr_t) * PageCount);
    if (!Pages) {
        return OsOutOfMemory;
    }
    
    if (Parameters->Flags & MEMORY_EXECUTABLE) {
        RequiredFlags |= MAPPING_EXECUTABLE;
    }
    if (!(Parameters->Flags & MEMORY_WRITE)) {
        RequiredFlags |= MAPPING_READONLY;
    }

    // Create the original mapping in the memory space passed, with the correct
    // access flags. The copied one must have all kinds of access.
    Status = MemorySpaceMap(MemorySpace, &Parameters->VirtualAddress, Pages,
        Parameters->Length, RequiredFlags, PlacementFlags);
    kfree(Pages);
    
    if (Status != OsSuccess) {
        ERROR("ScCreateMemorySpaceMapping::Failed the create mapping in original space");
        return Status;
    }
    
    // Create a cloned copy in our own memory space, however we will set new placement and
    // access flags
    PlacementFlags = MAPPING_VIRTUAL_PROCESS;
    RequiredFlags  = MAPPING_COMMIT | MAPPING_USERSPACE;
    Status         = CloneMemorySpaceMapping(MemorySpace, GetCurrentMemorySpace(),
        Parameters->VirtualAddress, &CopyPlacement, Parameters->Length,
        RequiredFlags, PlacementFlags);
    if (Status != OsSuccess) {
        ERROR("ScCreateMemorySpaceMapping::Failed the create mapping in parent space");
        MemorySpaceUnmap(MemorySpace, Parameters->VirtualAddress, Parameters->Length);
    }
    *AddressOut = (void*)CopyPlacement;
    return Status;
}
