/**
 * Copyright 2023, Philip Meulengracht
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
 */

#include <arch/mmu.h>
#include <arch/utils.h>
#include <assert.h>
#include <machine.h>
#include <heap.h>
#include <limits.h>
#include <string.h>
#include <threading.h>
#include "private.h"

oserr_t
MemorySpaceInitialize(
    _In_ MemorySpace_t*           memorySpace,
    _In_ struct VBoot*            bootInformation,
    _In_ PlatformMemoryMapping_t* kernelMappings)
{
    // Initialize the memory space handles.
    memorySpace->ParentHandle        = UUID_INVALID;
    memorySpace->GroupGlobalHandleID = UUID_INVALID;

    // Load the kernel memory mappings.
    return MmuLoadKernel(memorySpace, bootInformation, kernelMappings);
}

static oserr_t
__ChangeProtection(
    _In_    MemorySpace_t* memorySpace,
    _InOut_ vaddr_t        address,
    _In_    size_t         length,
    _In_    unsigned int   attributes,
    _Out_   unsigned int*  previousAttributes)
{
    int     pageCount = DIVUP((length + (address % GetMemorySpacePageSize())), GetMemorySpacePageSize());
    int     pagesUpdated;
    oserr_t oserr;

    if (!memorySpace || !length || !previousAttributes) {
        return OS_EINVALPARAMS;
    }

    *previousAttributes = attributes;
    oserr = ArchMmuUpdatePageAttributes(
            memorySpace,
            address,
            pageCount,
            previousAttributes,
            &pagesUpdated
    );
    if (oserr != OS_EOK && oserr != OS_EINCOMPLETE) {
        return oserr;
    }
    MSSync(memorySpace, address, length);
    return oserr;
}

static oserr_t
__ChangeSHMProtection(
    _In_  MemorySpace_t* memorySpace,
    _In_  vaddr_t        address,
    _In_  size_t         length,
    _In_  unsigned int   attributes,
    _Out_ unsigned int*  previousAttributes)
{
    size_t       pageSize = GetMemorySpacePageSize();
    size_t       pageCount = DIVUP(length + address % pageSize, pageSize);
    size_t       i;
    unsigned int previous;
    unsigned int ignored;
    oserr_t      status;

    if (previousAttributes == NULL) {
        return OS_EINVALPARAMS;
    }

    address &= ~(pageSize - 1);
    for (i = 0; i < pageCount; i++) {
        status = GetMemorySpaceAttributes(memorySpace, address, pageSize, &previous);
        if (status != OS_EOK) {
            return i == 0 ? status : OS_EINCOMPLETE;
        }

        if (i == 0) {
            *previousAttributes = previous;
        }

        // Keep each page's commit state when changing protection.
        status = __ChangeProtection(
            memorySpace,
            address,
            pageSize,
            attributes | (previous & MAPPING_COMMIT),
            &ignored
        );
        if (status != OS_EOK) {
            return i == 0 ? status : OS_EINCOMPLETE;
        }
        address += pageSize;
    }
    return OS_EOK;
}

oserr_t
MemorySpaceChangeProtection(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         length,
    _In_ unsigned int   attributes,
    _Out_ unsigned int* previousAttributes)
{
    struct MSContext*    context;
    struct MSAllocation* allocation;
    oserr_t              status;
    bool                 allowFreedPages;

    if (memorySpace == NULL) {
        return OS_EINVALPARAMS;
    }
    
    context = __MSContextGet(memorySpace);
    if (context != NULL) {
        MutexLock(&context->UserCopyLock);
    }
    
    allowFreedPages = false;
    status = MSAllocationFindSHM(
        context,
        address,
        length,
        allowFreedPages,
        &allocation
    );
    if (status != OS_EOK) {
        goto exit;
    }
    if (allocation != NULL) {
        status = SHMViewValidateMapping(allocation->SHM.View, allocation->SHM.Offset,
                                        allocation->Length, attributes);
        if (status != OS_EOK) {
            goto exit;
        }

        // Protection changes cannot change backing lifetime or cache policy.
        attributes &= MAPPING_READONLY;
        attributes |= MAPPING_USERSPACE | MAPPING_PERSISTENT;
        attributes |= allocation->Flags & MAPPING_NOCACHE;
        status = __ChangeSHMProtection(memorySpace, address, length, attributes, previousAttributes);
        goto exit;
    }

    status = __ChangeProtection(
        memorySpace,
        address,
        length,
        attributes,
        previousAttributes
    );

exit:
    if (context != NULL) {
        MutexUnlock(&context->UserCopyLock);
    }
    return status;
}

static bool
__IsAddressFreed(
    _In_ struct MSAllocation* allocation,
    _In_ vaddr_t              address)
{
    // Calculate the offset into the allocation
    vaddr_t offset = address - allocation->Address;
    size_t  index  = offset / GetMemorySpacePageSize();
    return bitmap_bits_set(&allocation->Pages, (int)index, 1);
}

oserr_t
MemorySpaceQuery(
    _In_ MemorySpace_t*        memorySpace,
    _In_ vaddr_t               address,
    _In_ OSMemoryDescriptor_t* descriptor)
{
    struct MSAllocation* allocation;
    vaddr_t              aligned = address & ~(GetMemorySpacePageSize() - 1);

    if (!memorySpace) {
        return OS_EINVALPARAMS;
    }

    allocation = MSAllocationLookup(__MSContextGet(memorySpace), address);
    if (!allocation) {
        return OS_ENOENT;
    }

    // Exclude the stack guard page from the reported start address.
    descriptor->StartAddress = allocation->Address;
    if (allocation->Flags & MAPPING_STACK) {
        descriptor->StartAddress += GetMemorySpacePageSize();
    }

    descriptor->SHMTag = allocation->SHMTag;
    descriptor->AllocationSize = allocation->Length;
    descriptor->Attributes = allocation->Flags;

    // A freed page still has allocation information. Set AllocationSize to 0
    // to mark this page as invalid.
    if (__IsAddressFreed(allocation, aligned)) {
        descriptor->AllocationSize = 0;
    }
    return OS_EOK;
}

oserr_t
GetMemorySpaceMapping(
    _In_  MemorySpace_t* memorySpace,
    _In_  vaddr_t        address,
    _In_  int            pageCount,
    _Out_ uintptr_t*     dmaVectorOut)
{
    int      pagesRetrieved;
    size_t   pageSize;
    oserr_t  oserr;

    oserr = ArchMmuVirtualToPhysical(
            memorySpace,
            address,
            pageCount,
            dmaVectorOut,
            &pagesRetrieved
    );
    if (oserr != OS_EOK && oserr != OS_EINCOMPLETE) {
        return oserr;
    }

    // Preserve the initial page offset in the first physical address.
    pageSize = GetMemorySpacePageSize();
    if (address & (pageSize - 1)) {
        dmaVectorOut[0] |= address & (pageSize - 1);
    }
    return oserr;
}

oserr_t
GetMemorySpaceAttributes(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ size_t         length,
    _In_ unsigned int*  attributesArray)
{
    int pageCount = DIVUP(length, GetMemorySpacePageSize());
    int pagesRetrieved;

    if (!memorySpace || !pageCount || !attributesArray) {
        return OS_EINVALPARAMS;
    }
    return ArchMmuGetPageAttributes(memorySpace, address, pageCount, attributesArray, &pagesRetrieved);
}

void
MemorySpaceSwitch(
        _In_ MemorySpace_t* memorySpace)
{
    assert(memorySpace != NULL);
    ArchMmuSwitchMemorySpace(memorySpace);
}

MemorySpace_t*
GetCurrentMemorySpace(void)
{
    Thread_t* currentThread = ThreadCurrentForCore(ArchGetProcessorCoreId());

    // Use the kernel address space when this core has no current thread.
    if (currentThread == NULL) {
        return GetDomainMemorySpace();
    } else {
        assert(ThreadMemorySpace(currentThread) != NULL);
        return ThreadMemorySpace(currentThread);
    }
}

uuid_t
GetCurrentMemorySpaceHandle(void)
{
    Thread_t* currentThread = ThreadCurrentForCore(ArchGetProcessorCoreId());
    if (currentThread == NULL) {
        return UUID_INVALID;
    } else {
        return ThreadMemorySpaceHandle(currentThread);
    }
}

MemorySpace_t*
GetDomainMemorySpace(void)
{
    return (GetCurrentDomain() != NULL) ?
           &GetCurrentDomain()->SystemSpace :
           &GetMachine()->SystemSpace;
}

oserr_t
AreMemorySpacesRelated(
        _In_ MemorySpace_t* Space1,
        _In_ MemorySpace_t* Space2)
{
    return (Space1->GroupGlobalHandleID == Space2->GroupGlobalHandleID) ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
MemorySpaceSetSignalHandler(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        signalHandlerAddress)
{
    struct MSContext* context;

    if (!memorySpace) {
        return OS_EINVALPARAMS;
    }

    context = __MSContextGet(memorySpace);
    if (context == NULL) {
        return OS_EINVALPARAMS;
    }

    context->SignalHandler = signalHandlerAddress;
    return OS_EOK;
}

vaddr_t
MemorySpaceSignalHandler(
        _In_ MemorySpace_t* memorySpace)
{
    struct MSContext* context;

    if (!memorySpace) {
        return 0;
    }

    context = __MSContextGet(memorySpace);
    return context != NULL ? context->SignalHandler : 0;
}

size_t
GetMemorySpacePageSize(void)
{
    return GetMachine()->MemoryGranularity;
}
