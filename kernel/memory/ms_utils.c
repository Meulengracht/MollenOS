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
#include <threading.h>
#include "private.h"

oserr_t
MemorySpaceInitialize(
        _In_ MemorySpace_t*           memorySpace,
        _In_ struct VBoot*            bootInformation,
        _In_ PlatformMemoryMapping_t* kernelMappings)
{
    // initialzie the data structure
    memorySpace->ParentHandle = UUID_INVALID;
    memorySpace->Context      = NULL;

    // initialize arch specific stuff
    return MmuLoadKernel(memorySpace, bootInformation, kernelMappings);
}

oserr_t
MemorySpaceChangeProtection(
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

oserr_t
MemorySpaceQuery(
        _In_ MemorySpace_t*        memorySpace,
        _In_ vaddr_t               address,
        _In_ OSMemoryDescriptor_t* descriptor)
{
    struct MSAllocation* allocation;

    if (!memorySpace) {
        return OS_EINVALPARAMS;
    }

    allocation = MSAllocationLookup(memorySpace->Context, address);
    if (!allocation) {
        return OS_ENOENT;
    }

    // If guard page was set, then adjust the start address, so we don't
    // promise the callers of this that the guard page is actually available.
    descriptor->StartAddress = allocation->Address;
    if (allocation->Flags & MAPPING_STACK) {
        descriptor->StartAddress += GetMemorySpacePageSize();
    }

    descriptor->SHMTag = allocation->SHMTag;
    descriptor->AllocationSize = allocation->Length;
    descriptor->Attributes = allocation->Flags;
    return OS_EOK;
}

oserr_t
GetMemorySpaceMapping(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        address,
        _In_  int            pageCount,
        _Out_ uintptr_t*     dmaVectorOut)
{
    oserr_t osStatus;
    int        pagesRetrieved;

    if (!memorySpace || !dmaVectorOut) {
        return OS_EINVALPARAMS;
    }

    osStatus = ArchMmuVirtualToPhysical(memorySpace, address, pageCount, dmaVectorOut, &pagesRetrieved);
    return osStatus;
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

oserr_t
IsMemorySpacePageDirty(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address)
{
    oserr_t   osStatus;
    unsigned int flags = 0;
    int          pagesRetrieved;

    if (!memorySpace) {
        return OS_EINVALPARAMS;
    }

    osStatus = ArchMmuGetPageAttributes(memorySpace, address, 1, &flags, &pagesRetrieved);
    if (osStatus == OS_EOK && !(flags & MAPPING_ISDIRTY)) {
        osStatus = OS_EUNKNOWN;
    }
    return osStatus;
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

    // if no threads are active return the kernel address space
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
    return (Space1->Context == Space2->Context) ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
IsMemorySpacePagePresent(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address)
{
    oserr_t   osStatus;
    unsigned int flags = 0;
    int          pagesRetrieved;

    if (!memorySpace) {
        return OS_EINVALPARAMS;
    }

    osStatus = ArchMmuGetPageAttributes(memorySpace, address, 1, &flags, &pagesRetrieved);
    if (osStatus == OS_EOK && !(flags & MAPPING_COMMIT)) {
        osStatus = OS_ENOENT;
    }
    return osStatus;
}

oserr_t
MemorySpaceSetSignalHandler(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        signalHandlerAddress)
{
    if (!memorySpace || !memorySpace->Context) {
        return OS_EINVALPARAMS;
    }

    memorySpace->Context->SignalHandler = signalHandlerAddress;
    return OS_EOK;
}

vaddr_t
MemorySpaceSignalHandler(
        _In_ MemorySpace_t* memorySpace)
{
    if (!memorySpace || !memorySpace->Context) {
        return 0;
    }
    return memorySpace->Context->SignalHandler;
}

size_t
GetMemorySpacePageSize(void)
{
    return GetMachine()->MemoryGranularity;
}
