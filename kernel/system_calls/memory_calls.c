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

#define __MODULE "sc_mem"
//#define __TRACE

#include <ddk/memory.h>

#include <os/mollenos.h>
#include <os/dmabuf.h>

#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <modules/manager.h>
#include <memoryspace.h>
#include <memory_region.h>
#include <threading.h>
#include <string.h>

OsStatus_t
ScMemoryAllocate(
    _In_      void*        hint,
    _In_      size_t       length,
    _In_      unsigned int flags,
    _Out_     void**       memoryOut)
{
    OsStatus_t     osStatus;
    uintptr_t      allocatedAddress;
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    unsigned int   memoryFlags    = MAPPING_USERSPACE;
    unsigned int   placementFlags = MAPPING_VIRTUAL_PROCESS;
    int            pageCount;
    uintptr_t*     pages;
    
    if (!length || !memoryOut) {
        return OsInvalidParameters;
    }

    pageCount = DIVUP(length, GetMemorySpacePageSize());
    pages     = kmalloc(sizeof(uintptr_t) * pageCount);
    if (!pages) {
        WARNING("[api] [mem_allocate] failed to allocate size 0x%llx, page count %i", length, pageCount);
        return OsOutOfMemory;
    }

    // Convert flags from memory domain to memory space domain
    if (flags & MEMORY_COMMIT) {
        memoryFlags |= MAPPING_COMMIT;
    }
    if (flags & MEMORY_UNCHACHEABLE) {
        memoryFlags |= MAPPING_NOCACHE;
    }
    if (flags & MEMORY_LOWFIRST) {
        memoryFlags |= MAPPING_LOWFIRST;
    }
    if (!(flags & MEMORY_WRITE)) {
        //MemoryFlags |= MAPPING_READONLY;
    }
    if (flags & MEMORY_EXECUTABLE) {
        memoryFlags |= MAPPING_EXECUTABLE;
    }
    
    // Create the actual mappings
    osStatus = MemorySpaceMap(memorySpace, &allocatedAddress, pages, length, memoryFlags, placementFlags);
    if (osStatus == OsSuccess) {
        *memoryOut = (void*)allocatedAddress;
        if ((flags & (MEMORY_COMMIT | MEMORY_CLEAN)) == (MEMORY_COMMIT | MEMORY_CLEAN)) {
            memset((void*)allocatedAddress, 0, length);
        }
    }

    TRACE("[sc_mem] [allocate] flags 0x%x, length 0x%" PRIxIN " == 0x%" PRIxIN,
          flags, length, allocatedAddress);
    kfree(pages);
    return osStatus;
}

OsStatus_t 
ScMemoryFree(
    _In_ uintptr_t  Address, 
    _In_ size_t     Size)
{
    MemorySpace_t * Space = GetCurrentMemorySpace();
    if (Address == 0 || Size == 0) {
        return OsInvalidParameters;
    }
    TRACE("[sc_mem] [unmap] address 0x%" PRIxIN ", length 0x%" PRIxIN, Address, Size);
    return MemorySpaceUnmap(Space, Address, Size);
}

OsStatus_t
ScMemoryProtect(
    _In_  void*    MemoryPointer,
    _In_  size_t   Length,
    _In_  unsigned int  Flags,
    _Out_ unsigned int* PreviousFlags)
{
    uintptr_t AddressStart = (uintptr_t)MemoryPointer;
    if (MemoryPointer == NULL || Length == 0) {
        return OsSuccess;
    }
    return MemorySpaceChangeProtection(GetCurrentMemorySpace(), 
        AddressStart, Length, Flags | MAPPING_USERSPACE, PreviousFlags);
}

OsStatus_t
ScDmaCreate(
    _In_ struct dma_buffer_info* info,
    _In_ struct dma_attachment*  attachment)
{
    OsStatus_t Status;
    unsigned int    Flags = 0;
    void*      KernelMapping;

    if (!info || !attachment) {
        return OsInvalidParameters;
    }
    
    TRACE("[sc_mem] [dma_create] %u, 0x%x", LODWORD(info->length), info->flags);

    if (info->flags & DMA_PERSISTANT) {
        Flags |= MAPPING_PERSISTENT;
    }
    
    if (info->flags & DMA_UNCACHEABLE) {
        Flags |= MAPPING_NOCACHE;
    }
    
    Status = MemoryRegionCreate(info->length, info->capacity, 
        Flags, &KernelMapping, &attachment->buffer, &attachment->handle);
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
    unsigned int    Flags = 0;

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
    
    Status = MemoryRegionCreateExisting(buffer, info->length,
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
    if (!attachment) {
        ERROR("[sc_dma_attach] null attachment pointer");
        return OsInvalidParameters;
    }
    
    // Update the attachment with info as it were correct
    attachment->handle = handle;
    attachment->length = 0;
    attachment->buffer = NULL;
    return MemoryRegionAttach(handle, &attachment->length);
}

OsStatus_t
ScDmaDetach(
    _In_ struct dma_attachment* Attachment)
{
    if (!Attachment) {
        return OsInvalidParameters;
    }
    DestroyHandle(Attachment->handle);
    return OsSuccess;
}

OsStatus_t
ScDmaRead(
    _In_  UUId_t  Handle,
    _In_  size_t  Offset,
    _In_  void*   Buffer,
    _In_  size_t  Length,
    _Out_ size_t* BytesRead)
{
    return MemoryRegionRead(Handle, Offset, Buffer, Length, BytesRead);
}

OsStatus_t
ScDmaWrite(
    _In_  UUId_t      Handle,
    _In_  size_t      Offset,
    _In_  const void* Buffer,
    _In_  size_t      Length,
    _Out_ size_t*     BytesWritten)
{
    return MemoryRegionWrite(Handle, Offset, Buffer, Length, BytesWritten);
}

OsStatus_t
ScDmaGetMetrics(
    _In_  UUId_t         Handle,
    _Out_ int*           SgCountOut,
    _Out_ struct dma_sg* SgListOut)
{
    return MemoryRegionGetSg(Handle, SgCountOut, SgListOut);
}

OsStatus_t
ScDmaAttachmentMap(
    _In_ struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return MemoryRegionInherit(attachment->handle, &attachment->buffer, &attachment->length);
}

OsStatus_t
ScDmaAttachmentResize(
    _In_ struct dma_attachment* attachment,
    _In_ size_t                 length)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return MemoryRegionResize(attachment->handle, attachment->buffer, length);
}

OsStatus_t
ScDmaAttachmentRefresh(
    _In_ struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return MemoryRegionRefresh(attachment->handle, attachment->buffer, 
        attachment->length, &attachment->length);
}

OsStatus_t
ScDmaAttachmentUnmap(
    _In_ struct dma_attachment* attachment)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return MemoryRegionUnherit(attachment->handle, attachment->buffer);
}

OsStatus_t 
ScCreateMemorySpace(
    _In_  unsigned int Flags,
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
    Thread_t*  Thread;
    SystemModule_t* Module = GetCurrentModule();
    if (Handle == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsInvalidPermissions;
        }
        return OsError;
    }
    Thread = THREAD_GET(ThreadHandle);
    if (Thread != NULL) {
        *Handle = ThreadMemorySpaceHandle(Thread);
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
    SystemModule_t*      Module = GetCurrentModule();
    MemorySpace_t*       MemorySpace = (MemorySpace_t*)LookupHandleOfType(Handle, HandleTypeMemorySpace);
    unsigned int         RequiredFlags  = MAPPING_COMMIT | MAPPING_USERSPACE;
    VirtualAddress_t     CopyPlacement  = 0;
    int                  PageCount;
    uintptr_t*           Pages;
    OsStatus_t           Status;
    TRACE("[sc_map] target address 0x%" PRIxIN ", flags 0x%x, length 0x%" PRIxIN,
        Parameters->VirtualAddress, Parameters->Flags, Parameters->Length);
    
    if (Parameters == NULL || AddressOut == NULL || Module == NULL) {
        if (Module == NULL) {
            return OsDoesNotExist;
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
        Parameters->Length, RequiredFlags, MAPPING_VIRTUAL_FIXED);
    kfree(Pages);
    
    if (Status != OsSuccess) {
        ERROR("ScCreateMemorySpaceMapping::Failed the create mapping in original space");
        return Status;
    }
    
    // Create a cloned copy in our own memory space, however we will set new placement and
    // access flags
    RequiredFlags  = MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT;
    Status         = CloneMemorySpaceMapping(MemorySpace, GetCurrentMemorySpace(),
        Parameters->VirtualAddress, &CopyPlacement, Parameters->Length,
        RequiredFlags, MAPPING_VIRTUAL_PROCESS);
    if (Status != OsSuccess) {
        ERROR("ScCreateMemorySpaceMapping::Failed the create mapping in parent space");
        MemorySpaceUnmap(MemorySpace, Parameters->VirtualAddress, Parameters->Length);
    }
    TRACE("[sc_map] local address 0x%" PRIxIN ", flags 0x%x, length 0x%" PRIxIN,
        CopyPlacement, RequiredFlags, Parameters->Length);
    *AddressOut = (void*)CopyPlacement;
    return Status;
}


OsStatus_t
ScMapThreadMemoryRegion(
    _In_  UUId_t    ThreadHandle,
    _In_  uintptr_t Address,
    _In_  size_t    Length,
    _Out_ void**    PointerOut)
{
    Thread_t*       thread;
    SystemModule_t* module = GetCurrentModule();
    uintptr_t       copiedAddress;
    OsStatus_t      status;

    if (!module) {
        return OsInvalidPermissions;
    }

    if (!Length) {
        return OsInvalidParameters;
    }

    thread = THREAD_GET(ThreadHandle);
    if (!thread) {
        return OsDoesNotExist;
    }

    status = CloneMemorySpaceMapping(ThreadMemorySpace(thread), GetCurrentMemorySpace(),
                                     Address, &copiedAddress, Length,
                                     MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT,
                                     MAPPING_VIRTUAL_PROCESS);
    if (status != OsSuccess) {
        return status;
    }

    *PointerOut = (void*)copiedAddress;
    return OsSuccess;
}
