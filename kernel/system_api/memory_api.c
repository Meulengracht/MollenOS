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

static void __ConvertToKernelMemoryFlags(
        _In_  unsigned int  userFlags,
        _Out_ unsigned int* memoryFlagsOut,
        _Out_ unsigned int* placementFlagsOut)
{
    unsigned int memoryFlags    = MAPPING_USERSPACE;
    unsigned int placementFlags = MAPPING_VIRTUAL_PROCESS;

    // Convert flags from memory domain to memory space domain
    if (userFlags & MEMORY_COMMIT)       { memoryFlags |= MAPPING_COMMIT; }
    if (userFlags & MEMORY_UNCHACHEABLE) { memoryFlags |= MAPPING_NOCACHE; }
    if (userFlags & MEMORY_LOWFIRST)     { memoryFlags |= MAPPING_LOWFIRST; }
    //if (!(userFlags & MEMORY_WRITE))   { memoryFlags |= MAPPING_READONLY; }
    if (userFlags & MEMORY_EXECUTABLE)   { memoryFlags |= MAPPING_EXECUTABLE; }

    *memoryFlagsOut = memoryFlags;
    *placementFlagsOut = placementFlags;
}

static unsigned int __ConvertToUserMemoryFlags(
        _In_  unsigned int memoryFlags)
{
    unsigned int flags = MEMORY_READ;

    if (memoryFlags & MAPPING_COMMIT)      { flags |= MEMORY_COMMIT; }
    if (memoryFlags & MAPPING_NOCACHE)     { flags |= MEMORY_UNCHACHEABLE; }
    if (memoryFlags & MAPPING_LOWFIRST)    { flags |= MEMORY_LOWFIRST; }
    if (!(memoryFlags & MAPPING_READONLY)) { flags |= MEMORY_WRITE; }
    if (memoryFlags & MAPPING_EXECUTABLE)  { flags |= MEMORY_EXECUTABLE; }
    if (memoryFlags & MAPPING_ISDIRTY)     { flags |= MEMORY_DIRTY; }

    return flags;
}

static OsStatus_t __PerformAllocation(
        _In_  MemorySpace_t* memorySpace,
        _In_  void*          hint,
        _In_  size_t         length,
        _In_  unsigned int   memoryFlags,
        _In_  unsigned int   placementFlags,
        _Out_ void**         memoryOut)
{
    uintptr_t  allocatedAddress;
    int        pageCount;
    uintptr_t* pages;
    OsStatus_t osStatus;

    pageCount = DIVUP(length, GetMemorySpacePageSize());
    pages     = kmalloc(sizeof(uintptr_t) * pageCount);
    if (!pages) {
        WARNING("[api] [mem_allocate] failed to allocate size 0x%llx, page count %i", length, pageCount);
        return OsOutOfMemory;
    }

    // handle commit act
    if (hint && ((memoryFlags & (MEMORY_COMMIT | MEMORY_FIXED)) == (MEMORY_COMMIT | MEMORY_FIXED))) {
        osStatus = MemorySpaceCommit(memorySpace, (vaddr_t)hint, pages, length, placementFlags);
        allocatedAddress = pages[0];
    }
    else {
        osStatus = MemorySpaceMap(memorySpace, &allocatedAddress, pages, length, memoryFlags, placementFlags);
    }

    if (osStatus == OsSuccess) {
        *memoryOut = (void*)allocatedAddress;
    }

    kfree(pages);
    return osStatus;
}

OsStatus_t
ScMemoryAllocate(
    _In_      void*        hint,
    _In_      size_t       length,
    _In_      unsigned int flags,
    _Out_     void**       memoryOut)
{
    unsigned int   memoryFlags;
    unsigned int   placementFlags;
    MemorySpace_t* memorySpace;
    OsStatus_t     osStatus;
    TRACE("ScMemoryAllocate(length=0x%" PRIxIN ", flags=%u)", length, flags);
    
    if (!length || !memoryOut) {
        return OsInvalidParameters;
    }

    memorySpace = GetCurrentMemorySpace();
    __ConvertToKernelMemoryFlags(flags, &memoryFlags, &placementFlags);

    // Is user requesting a clone operation or just a normal allocation
    if (flags & MEMORY_CLONE) {
        uintptr_t allocatedAddress;

        if (!hint) {
            return OsInvalidParameters;
        }

        osStatus = MemorySpaceCloneMapping(memorySpace, memorySpace, (vaddr_t)hint, &allocatedAddress,
                                           length, memoryFlags, placementFlags);
        if (osStatus == OsSuccess) {
            *memoryOut = (void*)allocatedAddress;
        }
    }
    else {
        osStatus = __PerformAllocation(memorySpace, hint, length, memoryFlags, placementFlags, memoryOut);
        if (osStatus == OsSuccess) {
            if ((flags & (MEMORY_COMMIT | MEMORY_CLEAN)) == (MEMORY_COMMIT | MEMORY_CLEAN)) {
                memset((void*)*memoryOut, 0, length);
            }
        }
    }
    TRACE("ScMemoryAllocate returns=0x%" PRIxIN, *memoryOut);
    return osStatus;
}

OsStatus_t 
ScMemoryFree(
    _In_ uintptr_t address,
    _In_ size_t    length)
{
    TRACE("ScMemoryFree(address=0x%" PRIxIN ", length=0x%" PRIxIN ")", address, length);
    if (!address || !length) {
        return OsInvalidParameters;
    }
    return MemorySpaceUnmap(GetCurrentMemorySpace(), address, length);
}

OsStatus_t
ScMemoryProtect(
    _In_  void*         memoryPointer,
    _In_  size_t        length,
    _In_  unsigned int  flags,
    _Out_ unsigned int* previousFlags)
{
    if (!memoryPointer || !length) {
        return OsSuccess;
    }
    return MemorySpaceChangeProtection(GetCurrentMemorySpace(),
                                       (uintptr_t)memoryPointer, length,
                                       flags | MAPPING_USERSPACE, previousFlags);
}

OsStatus_t
ScMemoryQueryAllocation(
        _In_ void*               memoryPointer,
        _In_ MemoryDescriptor_t* descriptor)
{
    OsStatus_t osStatus;

    if (!memoryPointer || !descriptor) {
        return OsInvalidParameters;
    }

    osStatus = MemorySpaceQuery(GetCurrentMemorySpace(), (vaddr_t)memoryPointer, descriptor);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    descriptor->Attributes = __ConvertToUserMemoryFlags(descriptor->Attributes);
    return OsSuccess;
}

OsStatus_t
ScMemoryQueryAttributes(
        _In_ void*         memoryPointer,
        _In_ size_t        length,
        _In_ unsigned int* attributesArray)
{
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    int            entries = DIVUP(length, GetMemorySpacePageSize());
    uintptr_t      address = (uintptr_t)memoryPointer;
    OsStatus_t     osStatus;
    int            i;

    if (!address || !entries || !attributesArray) {
        return OsInvalidParameters;
    }

    osStatus = GetMemorySpaceAttributes(memorySpace, address, length, attributesArray);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    for (i = 0; i < entries; i++) {
        attributesArray[i] = __ConvertToUserMemoryFlags(attributesArray[i]);
    }
    return OsSuccess;
}

OsStatus_t
ScDmaCreate(
    _In_ struct dma_buffer_info* info,
    _In_ struct dma_attachment*  attachment)
{
    OsStatus_t   osStatus;
    unsigned int flags = 0;
    void*        kernelMapping;

    if (!info || !attachment) {
        return OsInvalidParameters;
    }
    
    TRACE("[sc_mem] [dma_create] %u, 0x%x", LODWORD(info->length), info->flags);

    if (info->flags & DMA_PERSISTANT)  { flags |= MAPPING_PERSISTENT; }
    if (info->flags & DMA_UNCACHEABLE) { flags |= MAPPING_NOCACHE; }
    if (info->flags & DMA_TRAP)        { flags |= MAPPING_TRAPPAGE; }

    osStatus = MemoryRegionCreate(info->length, info->capacity,
                                  flags, &kernelMapping, &attachment->buffer,
                                  &attachment->handle);
    if (osStatus != OsSuccess) {
        return osStatus;
    }
    
    if (info->length && info->flags & DMA_CLEAN) {
        memset(attachment->buffer, 0, info->length);
    }

    attachment->length = info->length;
    return osStatus;
}

OsStatus_t
ScDmaExport(
    _In_ void*                   buffer,
    _In_ struct dma_buffer_info* info,
    _In_ struct dma_attachment*  attachment)
{
    OsStatus_t   osStatus;
    unsigned int flags = 0;

    if (!info || !attachment) {
        return OsInvalidParameters;
    }

    // DMA_TRAP not supported for exported buffers. Different rules apply.
    // DMA_CLEAN not supported for exported buffers. We will not touch preexisting ones.
    if (info->flags & DMA_PERSISTANT)  { flags |= MAPPING_PERSISTENT; }
    if (info->flags & DMA_UNCACHEABLE) { flags |= MAPPING_NOCACHE; }
    
    TRACE("ScDmaExport(0x%" PRIxIN ", %u)", buffer, LODWORD(info->length));

    osStatus = MemoryRegionCreateExisting(buffer, info->length,
                                          flags, &attachment->handle);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    attachment->buffer = buffer;
    attachment->length = info->length;
    return osStatus;
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
    _In_ struct dma_attachment* attachment,
    _In_ unsigned int           accessFlags)
{
    unsigned int memoryFlags = 0;
    if (!attachment) {
        return OsInvalidParameters;
    }

    if (!(accessFlags & DMA_ACCESS_WRITE))   { memoryFlags |= MAPPING_READONLY; }
    if (!(accessFlags & DMA_ACCESS_EXECUTE)) { memoryFlags |= MAPPING_EXECUTABLE; }

    return MemoryRegionInherit(attachment->handle, &attachment->buffer, &attachment->length, memoryFlags);
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
ScDmaAttachmentCommit(
        _In_ struct dma_attachment* attachment,
        _In_ void*                  address,
        _In_ size_t                 length)
{
    if (!attachment) {
        return OsInvalidParameters;
    }
    return MemoryRegionCommit(attachment->handle, attachment->buffer, address, length);
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
    unsigned int RequiredFlags = MAPPING_COMMIT | MAPPING_USERSPACE;
    vaddr_t      CopyPlacement = 0;
    int          PageCount;
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
    Status         = MemorySpaceCloneMapping(MemorySpace, GetCurrentMemorySpace(),
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
    _In_  UUId_t    threadHandle,
    _In_  uintptr_t stackPointer,
    _Out_ void**    topOfStack,
    _Out_ void**    pointerOut)
{
    Thread_t*       thread;
    SystemModule_t* module = GetCurrentModule();
    uintptr_t       copiedAddress;
    OsStatus_t      status;
    size_t          correctLength;

    if (!module) {
        return OsInvalidPermissions;
    }

    thread = THREAD_GET(threadHandle);
    if (!thread) {
        return OsDoesNotExist;
    }

    correctLength = (uintptr_t)ThreadContext(thread, THREADING_CONTEXT_LEVEL1) - stackPointer;
    status        = MemorySpaceCloneMapping(ThreadMemorySpace(thread), GetCurrentMemorySpace(),
                                            stackPointer, &copiedAddress, correctLength,
                                     MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT,
                                            MAPPING_VIRTUAL_PROCESS);
    if (status != OsSuccess) {
        return status;
    }

    // add the correct offset to the address
    copiedAddress += stackPointer & 0xFFF;

    *pointerOut = (void*)copiedAddress;
    *topOfStack = (void*)(copiedAddress + correctLength);
    return OsSuccess;
}
