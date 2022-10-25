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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Memory related system call implementations
 */

#define __MODULE "sc_mem"
//#define __TRACE

#include <arch/utils.h>
#include <ddk/memory.h>
#include <os/types/dma.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
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

static oserr_t __PerformAllocation(
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
    oserr_t osStatus;

    pageCount = DIVUP(length, GetMemorySpacePageSize());
    pages     = kmalloc(sizeof(uintptr_t) * pageCount);
    if (!pages) {
        WARNING("[api] [mem_allocate] failed to allocate size 0x%llx, page count %i", length, pageCount);
        return OS_EOOM;
    }

    // handle commit act
    if (hint && ((memoryFlags & (MEMORY_COMMIT | MEMORY_FIXED)) == (MEMORY_COMMIT | MEMORY_FIXED))) {
        osStatus = MemorySpaceCommit(memorySpace, (vaddr_t)hint, pages, length, 0, placementFlags);
        allocatedAddress = pages[0];
    }
    else {
        osStatus = MemorySpaceMap(memorySpace, &allocatedAddress, pages, length, 0, memoryFlags, placementFlags);
    }

    if (osStatus == OS_EOK) {
        *memoryOut = (void*)allocatedAddress;
    }

    kfree(pages);
    return osStatus;
}

oserr_t
ScMemoryAllocate(
    _In_      void*        hint,
    _In_      size_t       length,
    _In_      unsigned int flags,
    _Out_     void**       memoryOut)
{
    unsigned int   memoryFlags;
    unsigned int   placementFlags;
    MemorySpace_t* memorySpace;
    oserr_t     osStatus;
    TRACE("ScMemoryAllocate(length=0x%" PRIxIN ", flags=%u)", length, flags);
    
    if (!length || !memoryOut) {
        return OS_EINVALPARAMS;
    }

    memorySpace = GetCurrentMemorySpace();
    __ConvertToKernelMemoryFlags(flags, &memoryFlags, &placementFlags);

    // Is user requesting a clone operation or just a normal allocation
    if (flags & MEMORY_CLONE) {
        uintptr_t allocatedAddress;

        if (!hint) {
            return OS_EINVALPARAMS;
        }

        osStatus = MemorySpaceCloneMapping(memorySpace, memorySpace, (vaddr_t)hint, &allocatedAddress,
                                           length, memoryFlags, placementFlags);
        if (osStatus == OS_EOK) {
            *memoryOut = (void*)allocatedAddress;
        }
    }
    else {
        osStatus = __PerformAllocation(memorySpace, hint, length, memoryFlags, placementFlags, memoryOut);
        if (osStatus == OS_EOK) {
            if ((flags & (MEMORY_COMMIT | MEMORY_CLEAN)) == (MEMORY_COMMIT | MEMORY_CLEAN)) {
                memset((void*)*memoryOut, 0, length);
            }
        }
    }
    TRACE("ScMemoryAllocate returns=0x%" PRIxIN, *memoryOut);
    return osStatus;
}

oserr_t
ScMemoryFree(
    _In_ uintptr_t address,
    _In_ size_t    length)
{
    TRACE("ScMemoryFree(address=0x%" PRIxIN ", length=0x%" PRIxIN ")", address, length);
    if (!address || !length) {
        return OS_EINVALPARAMS;
    }
    return MemorySpaceUnmap(GetCurrentMemorySpace(), address, length);
}

oserr_t
ScMemoryProtect(
    _In_  void*         memoryPointer,
    _In_  size_t        length,
    _In_  unsigned int  flags,
    _Out_ unsigned int* previousFlags)
{
    if (!memoryPointer || !length) {
        return OS_EOK;
    }
    return MemorySpaceChangeProtection(GetCurrentMemorySpace(),
                                       (uintptr_t)memoryPointer, length,
                                       flags | MAPPING_USERSPACE, previousFlags);
}

oserr_t
ScMemoryQueryAllocation(
        _In_ void*                 memoryPointer,
        _In_ OsMemoryDescriptor_t* descriptor)
{
    oserr_t osStatus;

    if (!memoryPointer || !descriptor) {
        return OS_EINVALPARAMS;
    }

    osStatus = MemorySpaceQuery(GetCurrentMemorySpace(), (vaddr_t)memoryPointer, descriptor);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    descriptor->Attributes = __ConvertToUserMemoryFlags(descriptor->Attributes);
    return OS_EOK;
}

oserr_t
ScMemoryQueryAttributes(
        _In_ void*         memoryPointer,
        _In_ size_t        length,
        _In_ unsigned int* attributesArray)
{
    MemorySpace_t* memorySpace = GetCurrentMemorySpace();
    int            entries = DIVUP(length, GetMemorySpacePageSize());
    uintptr_t      address = (uintptr_t)memoryPointer;
    oserr_t     osStatus;
    int            i;

    if (!address || !entries || !attributesArray) {
        return OS_EINVALPARAMS;
    }

    osStatus = GetMemorySpaceAttributes(memorySpace, address, length, attributesArray);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    for (i = 0; i < entries; i++) {
        attributesArray[i] = __ConvertToUserMemoryFlags(attributesArray[i]);
    }
    return OS_EOK;
}

oserr_t
ScDmaCreate(
    _In_ DMABuffer_t*     info,
    _In_ DMAAttachment_t* attachment)
{
    oserr_t   osStatus;
    unsigned int flags = 0;
    size_t       pageMask;
    void*        kernelMapping;

    if (!info || !attachment) {
        return OS_EINVALPARAMS;
    }
    
    TRACE("[sc_mem] [DmaCreate] %u, 0x%x", LODWORD(info->length), info->flags);
    osStatus = ArchGetPageMaskFromDmaType(info->type, &pageMask);
    if (osStatus != OS_EOK) {
        ERROR("ScDmaCreate unsupported dma buffer type %u on this platform", info->type);
        return osStatus;
    }

    if (info->flags & DMA_PERSISTANT)  { flags |= MAPPING_PERSISTENT; }
    if (info->flags & DMA_UNCACHEABLE) { flags |= MAPPING_NOCACHE; }
    if (info->flags & DMA_TRAP)        { flags |= MAPPING_TRAPPAGE; }

    osStatus = MemoryRegionCreate(
            info->length,
            info->capacity,
            flags,
            pageMask,
            &kernelMapping,
            &attachment->buffer,
            &attachment->handle
    );
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    
    if (info->length && info->flags & DMA_CLEAN) {
        memset(attachment->buffer, 0, info->length);
    }

    attachment->length = info->length;
    return osStatus;
}

oserr_t
ScDmaExport(
    _In_ void*            buffer,
    _In_ DMABuffer_t*     info,
    _In_ DMAAttachment_t* attachment)
{
    oserr_t   osStatus;
    unsigned int flags = 0;

    if (!info || !attachment) {
        return OS_EINVALPARAMS;
    }

    // DMA_TRAP not supported for exported buffers. Different rules apply.
    // DMA_CLEAN not supported for exported buffers. We will not touch preexisting ones.
    if (info->flags & DMA_PERSISTANT)  { flags |= MAPPING_PERSISTENT; }
    if (info->flags & DMA_UNCACHEABLE) { flags |= MAPPING_NOCACHE; }
    
    TRACE("ScDmaExport(0x%" PRIxIN ", %u)", buffer, LODWORD(info->length));

    osStatus = MemoryRegionCreateExisting(buffer, info->length,
                                          flags, &attachment->handle);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    attachment->buffer = buffer;
    attachment->length = info->length;
    return osStatus;
}

oserr_t
ScDmaAttach(
        _In_ uuid_t           handle,
        _In_ DMAAttachment_t* attachment)
{
    if (!attachment) {
        ERROR("[sc_dma_attach] null attachment pointer");
        return OS_EINVALPARAMS;
    }
    
    // Update the attachment with info as it were correct
    attachment->handle = handle;
    attachment->length = 0;
    attachment->buffer = NULL;
    return MemoryRegionAttach(handle, &attachment->length);
}

oserr_t
ScDmaDetach(
    _In_ DMAAttachment_t* attachment)
{
    if (!attachment) {
        return OS_EINVALPARAMS;
    }
    DestroyHandle(attachment->handle);
    return OS_EOK;
}

oserr_t
ScDmaRead(
        _In_  uuid_t  Handle,
        _In_  size_t  Offset,
        _In_  void*   Buffer,
        _In_  size_t  Length,
        _Out_ size_t* BytesRead)
{
    return MemoryRegionRead(Handle, Offset, Buffer, Length, BytesRead);
}

oserr_t
ScDmaWrite(
        _In_  uuid_t      Handle,
        _In_  size_t      Offset,
        _In_  const void* Buffer,
        _In_  size_t      Length,
        _Out_ size_t*     BytesWritten)
{
    return MemoryRegionWrite(Handle, Offset, Buffer, Length, BytesWritten);
}

oserr_t
ScDmaGetMetrics(
        _In_  uuid_t   Handle,
        _Out_ int*     SgCountOut,
        _Out_ DMASG_t* SgListOut)
{
    return MemoryRegionGetSg(Handle, SgCountOut, SgListOut);
}

oserr_t
ScDmaAttachmentMap(
    _In_ DMAAttachment_t* attachment,
    _In_ unsigned int           accessFlags)
{
    unsigned int memoryFlags = 0;
    if (!attachment) {
        return OS_EINVALPARAMS;
    }

    if (!(accessFlags & DMA_ACCESS_WRITE))   { memoryFlags |= MAPPING_READONLY; }
    if (!(accessFlags & DMA_ACCESS_EXECUTE)) { memoryFlags |= MAPPING_EXECUTABLE; }

    return MemoryRegionInherit(attachment->handle, &attachment->buffer, &attachment->length, memoryFlags);
}

oserr_t
ScDmaAttachmentResize(
    _In_ DMAAttachment_t* attachment,
    _In_ size_t           length)
{
    if (!attachment) {
        return OS_EINVALPARAMS;
    }
    return MemoryRegionResize(attachment->handle, attachment->buffer, length);
}

oserr_t
ScDmaAttachmentRefresh(
    _In_ DMAAttachment_t* attachment)
{
    if (!attachment) {
        return OS_EINVALPARAMS;
    }
    return MemoryRegionRefresh(attachment->handle, attachment->buffer, 
        attachment->length, &attachment->length);
}

oserr_t
ScDmaAttachmentCommit(
        _In_ DMAAttachment_t* attachment,
        _In_ void*            address,
        _In_ size_t           length)
{
    if (!attachment) {
        return OS_EINVALPARAMS;
    }
    return MemoryRegionCommit(attachment->handle, attachment->buffer, address, length);
}

oserr_t
ScDmaAttachmentUnmap(
    _In_ DMAAttachment_t* attachment)
{
    if (!attachment) {
        return OS_EINVALPARAMS;
    }
    return MemoryRegionUnherit(attachment->handle, attachment->buffer);
}

oserr_t
ScCreateMemorySpace(
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut)
{
    if (handleOut == NULL) {
        return OS_EUNKNOWN;
    }
    return CreateMemorySpace(flags | MEMORY_SPACE_APPLICATION, handleOut);
}

oserr_t
ScGetThreadMemorySpaceHandle(
        _In_  uuid_t  threadHandle,
        _Out_ uuid_t* handleOut)
{
    Thread_t* thread;
    if (handleOut == NULL) {
        return OS_EUNKNOWN;
    }

    thread = THREAD_GET(threadHandle);
    if (thread != NULL) {
        *handleOut = ThreadMemorySpaceHandle(thread);
        return OS_EOK;
    }
    return OS_ENOENT;
}

oserr_t
ScCreateMemorySpaceMapping(
        _In_  uuid_t                          handle,
        _In_  struct MemoryMappingParameters* mappingParameters,
        _Out_ void**                          addressOut)
{
    MemorySpace_t* memorySpace = (MemorySpace_t*)LookupHandleOfType(handle, HandleTypeMemorySpace);
    unsigned int   memoryFlags   = MAPPING_COMMIT | MAPPING_USERSPACE;
    vaddr_t        copyPlacement = 0;
    int            pageCount;
    uintptr_t*     pages;
    oserr_t     osStatus;
    TRACE("[sc_map] target address 0x%" PRIxIN ", flags 0x%x, length 0x%" PRIxIN,
          mappingParameters->VirtualAddress, mappingParameters->Flags, mappingParameters->Length);
    
    if (mappingParameters == NULL || addressOut == NULL) {
        return OS_EINVALPARAMS;
    }
    
    if (memorySpace == NULL) {
        return OS_ENOENT;
    }

    pageCount = DIVUP(mappingParameters->Length, GetMemorySpacePageSize());
    pages     = kmalloc(sizeof(uintptr_t) * pageCount);
    if (!pages) {
        return OS_EOOM;
    }
    
    if (mappingParameters->Flags & MEMORY_EXECUTABLE) {
        memoryFlags |= MAPPING_EXECUTABLE;
    }
    if (!(mappingParameters->Flags & MEMORY_WRITE)) {
        memoryFlags |= MAPPING_READONLY;
    }

    // Create the original mapping in the memory space passed, with the correct
    // access flags. The copied one must have all kinds of access.
    osStatus = MemorySpaceMap(memorySpace, &mappingParameters->VirtualAddress, pages,
                              mappingParameters->Length, 0, memoryFlags, MAPPING_VIRTUAL_FIXED);
    kfree(pages);
    
    if (osStatus != OS_EOK) {
        ERROR("ScCreateMemorySpaceMapping::Failed the create mapping in original space");
        return osStatus;
    }
    
    // Create a cloned copy in our own memory space, however we will set new placement and
    // access flags
    memoryFlags = MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT;
    osStatus = MemorySpaceCloneMapping(memorySpace, GetCurrentMemorySpace(),
                                       mappingParameters->VirtualAddress, &copyPlacement, mappingParameters->Length,
                                       memoryFlags, MAPPING_VIRTUAL_PROCESS);
    if (osStatus != OS_EOK) {
        ERROR("ScCreateMemorySpaceMapping::Failed the create mapping in parent space");
        MemorySpaceUnmap(memorySpace, mappingParameters->VirtualAddress, mappingParameters->Length);
    }
    TRACE("[sc_map] local address 0x%" PRIxIN ", flags 0x%x, length 0x%" PRIxIN,
          copyPlacement, memoryFlags, mappingParameters->Length);
    *addressOut = (void*)copyPlacement;
    return osStatus;
}


oserr_t
ScMapThreadMemoryRegion(
        _In_  uuid_t    threadHandle,
        _In_  uintptr_t stackPointer,
        _Out_ void**    topOfStack,
        _Out_ void**    pointerOut)
{
    Thread_t*  thread;
    uintptr_t  copiedAddress;
    oserr_t    status;
    size_t     correctLength;

    thread = THREAD_GET(threadHandle);
    if (!thread) {
        return OS_ENOENT;
    }

    correctLength = (uintptr_t)ThreadContext(thread, THREADING_CONTEXT_LEVEL1) - stackPointer;
    status        = MemorySpaceCloneMapping(ThreadMemorySpace(thread), GetCurrentMemorySpace(),
                                            stackPointer, &copiedAddress, correctLength,
                                     MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT,
                                            MAPPING_VIRTUAL_PROCESS);
    if (status != OS_EOK) {
        return status;
    }

    // add the correct offset to the address
    copiedAddress += stackPointer & 0xFFF;

    *pointerOut = (void*)copiedAddress;
    *topOfStack = (void*)(copiedAddress + correctLength);
    return OS_EOK;
}
