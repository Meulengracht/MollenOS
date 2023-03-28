/**
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
#include <os/types/shm.h>
#include <os/types/syscall.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <shm.h>
#include <threading.h>
#include <string.h>

static void
__ConvertToKernelMemoryFlags(
        _In_  unsigned int  userFlags,
        _Out_ unsigned int* memoryFlagsOut,
        _Out_ unsigned int* placementFlagsOut)
{
    unsigned int memoryFlags    = MAPPING_USERSPACE;
    unsigned int placementFlags = 0;

    // Convert flags from memory domain to memory space domain
    if (userFlags & MEMORY_COMMIT)       { memoryFlags |= MAPPING_COMMIT; }
    if (userFlags & MEMORY_CLEAN)        { memoryFlags |= MAPPING_CLEAN; }
    if (userFlags & MEMORY_UNCHACHEABLE) { memoryFlags |= MAPPING_NOCACHE; }

    if (userFlags & MEMORY_FIXED)        { placementFlags |= MAPPING_VIRTUAL_FIXED; }
    else                                 { placementFlags |= MAPPING_VIRTUAL_PROCESS; }

    if (userFlags & MEMORY_STACK)        { memoryFlags |= MAPPING_STACK; }

    //if (!(userFlags & MEMORY_WRITE))   { memoryFlags |= MAPPING_READONLY; }
    if (userFlags & MEMORY_EXECUTABLE)   { memoryFlags |= MAPPING_EXECUTABLE; }

    *memoryFlagsOut = memoryFlags;
    *placementFlagsOut = placementFlags;
}

static unsigned int
__ConvertToUserMemoryFlags(
        _In_ unsigned int memoryFlags)
{
    unsigned int flags = MEMORY_READ;

    if (memoryFlags & MAPPING_COMMIT)      { flags |= MEMORY_COMMIT; }
    if (memoryFlags & MAPPING_NOCACHE)     { flags |= MEMORY_UNCHACHEABLE; }
    if (!(memoryFlags & MAPPING_READONLY)) { flags |= MEMORY_WRITE; }
    if (memoryFlags & MAPPING_EXECUTABLE)  { flags |= MEMORY_EXECUTABLE; }
    if (memoryFlags & MAPPING_ISDIRTY)     { flags |= MEMORY_DIRTY; }

    return flags;
}

static oserr_t
__PerformAllocation(
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
    oserr_t    oserr;

    pageCount = DIVUP(length, GetMemorySpacePageSize());
    pages     = kmalloc(sizeof(uintptr_t) * pageCount);
    if (!pages) {
        WARNING("__PerformAllocation: cannot allocate size 0x%" PRIxIN ", page count %i", length, pageCount);
        return OS_EOOM;
    }

    oserr = MemorySpaceMap(
            memorySpace,
            &(struct MemorySpaceMapOptions) {
                .VirtualStart = (vaddr_t)hint,
                .Pages = pages,
                .Length = length,
                .Mask = __MASK,
                .Flags = memoryFlags,
                .PlacementFlags = placementFlags
            },
            &allocatedAddress
    );
    if (oserr == OS_EOK) {
        *memoryOut = (void*)allocatedAddress;
    }
    kfree(pages);
    return oserr;
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
    oserr_t        oserr;
    TRACE("ScMemoryAllocate(length=0x%" PRIxIN ", flags=%u)", length, flags);
    
    if (!length || !memoryOut) {
        return OS_EINVALPARAMS;
    }

    memorySpace = GetCurrentMemorySpace();
    __ConvertToKernelMemoryFlags(
            flags,
            &memoryFlags,
            &placementFlags
    );

    // Is user requesting a clone operation or just a normal allocation
    if (flags & MEMORY_CLONE) {
        uintptr_t allocatedAddress;

        if (!hint) {
            return OS_EINVALPARAMS;
        }

        oserr = MemorySpaceCloneMapping(
                memorySpace,
                memorySpace,
                (vaddr_t)hint,
                &allocatedAddress,
                length,
                memoryFlags,
                placementFlags
        );
        if (oserr == OS_EOK) {
            *memoryOut = (void*)allocatedAddress;
        }
    } else {
        oserr = __PerformAllocation(memorySpace, hint, length, memoryFlags, placementFlags, memoryOut);
    }
    TRACE("ScMemoryAllocate returns=0x%" PRIxIN, *memoryOut);
    return oserr;
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
        _In_ OSMemoryDescriptor_t* descriptor)
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
ScSHMCreate(
    _In_ SHM_t*       shm,
    _In_ SHMHandle_t* handle)
{
    return SHMCreate(shm, handle);
}

oserr_t
ScSHMExport(
    _In_ void*        buffer,
    _In_ SHM_t*       shm,
    _In_ SHMHandle_t* handle)
{
    if (shm == NULL) {
        return OS_EINVALPARAMS;
    }

    return SHMExport(
            buffer,
            shm->Size,
            shm->Flags,
            shm->Access,
            handle
    );
}

oserr_t
ScSHMConform(
        _In_ uuid_t                    shmID,
        _In_ OSSHMConformParameters_t* parameters,
        _In_ SHMHandle_t*              handle)
{
    if (parameters == NULL) {
        return OS_EINVALPARAMS;
    }

    return SHMConform(
            shmID,
            parameters->Conformity,
            parameters->Flags,
            parameters->Access,
            parameters->Offset,
            parameters->Length,
            handle
    );
}

oserr_t
ScSHMAttach(
        _In_ uuid_t       shmID,
        _In_ SHMHandle_t* handle)
{
    return SHMAttach(shmID, handle);
}

oserr_t
ScSHMDetach(
    _In_ SHMHandle_t* handle)
{
    return SHMDetach(handle);
}

oserr_t
ScSHMMetrics(
        _In_  uuid_t   shmID,
        _Out_ int*     sgCountOut,
        _Out_ SHMSG_t* sgOut)
{
    return SHMBuildSG(shmID, sgCountOut, sgOut);
}

oserr_t
ScSHMMap(
    _In_ SHMHandle_t* handle,
    _In_ size_t       offset,
    _In_ size_t       length,
    _In_ unsigned int flags)
{
    return SHMMap(
            handle,
            offset,
            length,
            flags
    );
}

oserr_t
ScSHMCommit(
        _In_ SHMHandle_t* handle,
        _In_ void*        address,
        _In_ size_t       length)
{
    if (handle == NULL) {
        return OS_EINVALPARAMS;
    }
    return SHMCommit(
            handle->ID,
            handle->Buffer,
            address,
            length
    );
}

oserr_t
ScSHMUnmap(
    _In_ SHMHandle_t* handle,
    _In_ void*        address,
    _In_ size_t       length)
{
    return SHMUnmap(handle, (vaddr_t)address, length);
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
    oserr_t        oserr;
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
    oserr = MemorySpaceMap(
            memorySpace,
            &(struct MemorySpaceMapOptions) {
                .VirtualStart = mappingParameters->VirtualAddress,
                .Pages = pages,
                .Length = mappingParameters->Length,
                .Mask = __MASK,
                .Flags = memoryFlags,
                .PlacementFlags = MAPPING_VIRTUAL_FIXED
            },
            &mappingParameters->VirtualAddress
    );
    kfree(pages);
    
    if (oserr != OS_EOK) {
        ERROR("ScCreateMemorySpaceMapping::Failed the create mapping in original space");
        return oserr;
    }
    
    // Create a cloned copy in our own memory space, however we will set new placement and
    // access flags
    memoryFlags = MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT;
    oserr = MemorySpaceCloneMapping(
            memorySpace,
            GetCurrentMemorySpace(),
            mappingParameters->VirtualAddress,
            &copyPlacement,
            mappingParameters->Length,
            memoryFlags,
            MAPPING_VIRTUAL_PROCESS
    );
    if (oserr != OS_EOK) {
        ERROR("ScCreateMemorySpaceMapping::Failed the create mapping in parent space");
        MemorySpaceUnmap(memorySpace, mappingParameters->VirtualAddress, mappingParameters->Length);
    }
    TRACE("[sc_map] local address 0x%" PRIxIN ", flags 0x%x, length 0x%" PRIxIN,
          copyPlacement, memoryFlags, mappingParameters->Length);
    *addressOut = (void*)copyPlacement;
    return oserr;
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

    thread = THREAD_GET(threadHandle);
    if (!thread) {
        return OS_ENOENT;
    }

    // Map in the first page of the stack
    status = MemorySpaceCloneMapping(
            ThreadMemorySpace(thread),
            GetCurrentMemorySpace(),
            stackPointer,
            &copiedAddress,
            GetMemorySpacePageSize() * 2,
            MAPPING_COMMIT | MAPPING_USERSPACE | MAPPING_PERSISTENT,
            MAPPING_VIRTUAL_PROCESS
    );
    if (status != OS_EOK) {
        return status;
    }

    *pointerOut = (void*)(copiedAddress + (stackPointer & 0xFFF));
    *topOfStack = (void*)(copiedAddress + (GetMemorySpacePageSize() * 2));
    return OS_EOK;
}
