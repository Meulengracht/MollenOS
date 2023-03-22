/**
 * MollenOS
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Memory Space Interface
 * - Implementation of virtual memory address spaces. This underlying
 *   hardware must support the __OSCONFIG_HAS_MMIO define to use this.
 */

#define __MODULE "MEM2"

//#define __TRACE

#include <arch/mmu.h>
#include <arch/utils.h>
#include <debug.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <string.h>
#include <threading.h>
#include "private.h"

#define __PMTYPE(_flags) ((_flags) & MAPPING_PHYSICAL_MASK)
#define __VMTYPE(_flags) ((_flags) & MAPPING_VIRTUAL_MASK)

static oserr_t
__VerifyFixedVirtualAddress(
        _In_  struct MemorySpaceMapOptions* options,
        _Out_ vaddr_t*                      baseAddressOut)
{
    if (options->VirtualStart == 0) {
        ERROR("__AllocateVirtualMemory: MAPPING_VIRTUAL_FIXED was set, but options->VirtualStart not provided");
        return OS_EINVALPARAMS;
    }

    // With pre-provided virtual regions for stacks, the region size *must* be
    // atleast two pages long. Otherwise, there is not enough room for the guard
    // page in the mapping.
    if ((options->Flags & MAPPING_STACK) && options->Length <= GetMemorySpacePageSize()) {
        return OS_EINVALPARAMS;
    }

    // Align the provided value on a page-boundary
    *baseAddressOut = options->VirtualStart & ~(GetMemorySpacePageSize() - 1);

    // Now fixup the allocated address if the allocation was a stack, but not
    // for pre-provided
    if (options->Flags & MAPPING_STACK) {
        *baseAddressOut += GetMemorySpacePageSize();
    }
    return OS_EOK;
}

static vaddr_t
__AllocateProcessMemory(
        _In_ MemorySpace_t* memorySpace,
        _In_ uuid_t         shmTag,
        _In_ size_t         size,
        _In_ unsigned int   mapFlags)
{
    vaddr_t address;

    if (memorySpace->Context == NULL) {
        ERROR("__AllocateProcessMemory: requested process memory for non-process memory space");
        return 0;
    }

    address = DynamicMemoryPoolAllocate(&memorySpace->Context->Heap, size);
    if (address == 0) {
        ERROR("__AllocateProcessMemory: cannot allocate 0x%" PRIxIN " bytes of memory", size);
    } else {
        // We only track user allocations, not kernel allocations. If we wanted to track ALL allocations
        // then we would have to guard against eternal loops as-well as the __CreateAllocation actually calls
        // kmalloc
        oserr_t oserr = MSAllocationCreate(memorySpace, shmTag, address, size, mapFlags);
        if (oserr != OS_EOK) {
            ERROR("__AllocateProcessMemory: cannot register allocation");
            DynamicMemoryPoolFree(&memorySpace->Context->Heap, size);
            address = 0;
        }
    }
    return address;
}

static oserr_t
__AllocateVirtualMemory(
        _In_  MemorySpace_t*                memorySpace,
        _In_  struct MemorySpaceMapOptions* options,
        _Out_ vaddr_t*                      baseAddressOut)
{
    vaddr_t virtualBase;
    size_t  length = options->Length;

    // Is this a stack allocation? Then we need to allocate another page
    // for the allocation, which will be the first page in the segment of memory.
    // this segment will be unmapped, but allocated in virtual memory.
    if (options->Flags & MAPPING_STACK) {
        length += GetMemorySpacePageSize();
    }

    switch (__VMTYPE(options->PlacementFlags)) {
        case MAPPING_VIRTUAL_FIXED: {
            return __VerifyFixedVirtualAddress(options, baseAddressOut);
        } break;

        case MAPPING_VIRTUAL_PROCESS: {
            if (!(memorySpace->Flags & MEMORY_SPACE_APPLICATION)) {
                ERROR("__AllocateVirtualMemory cannot allocate process memory for non-userspace memory space");
                return OS_ENOTSUPPORTED;
            }
            virtualBase = __AllocateProcessMemory(memorySpace, options->SHMTag, length, options->Flags);
        } break;

        case MAPPING_VIRTUAL_THREAD: {
            if (!(memorySpace->Flags & MEMORY_SPACE_APPLICATION)) {
                ERROR("__AllocateVirtualMemory cannot allocate TLS memory for non-userspace memory space");
                return OS_ENOTSUPPORTED;
            }
            virtualBase = DynamicMemoryPoolAllocate(&memorySpace->ThreadMemory, length);
        } break;

        case MAPPING_VIRTUAL_GLOBAL: {
            virtualBase = StaticMemoryPoolAllocate(&GetMachine()->GlobalAccessMemory, length);
        } break;

        default: {
            ERROR("__AllocateVirtualMemory: cannot allocate virtual memory for flags: 0x%x", options->PlacementFlags);
            return OS_EINVALPARAMS;
        } break;
    }

    if (virtualBase == 0) {
        ERROR("__AllocateVirtualMemory: cannot allocate size 0x%" PRIxIN, length);
        return OS_EOOM;
    }

    // Now fixup the allocated address if the allocation was a stack, but not
    // for pre-provided
    if (options->Flags & MAPPING_STACK) {
        virtualBase += GetMemorySpacePageSize();
    }

    *baseAddressOut = virtualBase;
    return OS_EOK;
}

static void
__FreeVirtualMemory(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size,
        _In_ unsigned int   memoryFlags,
        _In_ unsigned int   placementFlags)
{
    // Is this a stack allocation? Then we need to allocate another page
    // for the allocation, which will be the first page in the segment of memory.
    // this segment will be unmapped, but allocated in virtual memory.
    if (memoryFlags & MAPPING_STACK) {
        address -= GetMemorySpacePageSize();
        size += GetMemorySpacePageSize();
    }

    _CRT_UNUSED(size);
    switch (__VMTYPE(placementFlags)) {
        case MAPPING_VIRTUAL_PROCESS: {
            DynamicMemoryPoolFree(&memorySpace->Context->Heap, address);
        } break;

        case MAPPING_VIRTUAL_THREAD: {
            DynamicMemoryPoolFree(&memorySpace->ThreadMemory, address);
        } break;

        case MAPPING_VIRTUAL_GLOBAL: {
            StaticMemoryPoolFree(&GetMachine()->GlobalAccessMemory, address);
        } break;

        default:
            break;
    }
}

static oserr_t
__ReserveMapping(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        virtualBase,
        _In_ unsigned int   flags,
        _In_ int            pageCount)
{
    int     pagesReserved;
    oserr_t oserr;
    TRACE("__ReserveMapping(pageCount=%i)", pageCount);

    oserr = ArchMmuReserveVirtualPages(
            memorySpace,
            virtualBase,
            pageCount,
            flags,
            &pagesReserved
    );
    if (oserr != OS_EOK) {
        int freedCount, pagesClearedCount;
        (void)ArchMmuClearVirtualPages(
                memorySpace,
                virtualBase,
                pagesReserved,
                NULL,
                &freedCount,
                &pagesClearedCount
        );
    }
    return oserr;
}

static oserr_t
__CommitMapping(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ paddr_t*       pages,
        _In_ int            pageCount,
        _In_ unsigned int   flags)
{
    int     pagesUpdated;
    oserr_t oserr;

    oserr = ArchMmuSetVirtualPages(
            memorySpace,
            address,
            pages,
            pageCount,
            flags,
            &pagesUpdated
    );
    if (oserr != OS_EOK) {
        int freedCount, pagesClearedCount;
        (void)ArchMmuClearVirtualPages(
                memorySpace,
                address,
                pagesUpdated,
                NULL,
                &freedCount,
                &pagesClearedCount
        );
        return oserr;
    }

    if (flags & MAPPING_CLEAN) {
        memset(
                (void*)address,
                0,
                (pagesUpdated * GetMemorySpacePageSize())
        );
    }
    return OS_EOK;
}

static oserr_t
__ContiguousMapping(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        virtualStart,
        _In_ paddr_t        physicalStart,
        _In_ int            pageCount,
        _In_ unsigned int   flags)
{
    int     pagesUpdated;
    oserr_t oserr;

    oserr = ArchMmuSetContiguousVirtualPages(
            memorySpace,
            virtualStart,
            physicalStart,
            pageCount,
            flags | MAPPING_COMMIT,
            &pagesUpdated
    );
    if (oserr != OS_EOK) {
        int freedCount, pagesClearedCount;
        (void)ArchMmuClearVirtualPages(
                memorySpace,
                virtualStart,
                pagesUpdated,
                NULL,
                &freedCount,
                &pagesClearedCount
        );
    }
    return oserr;
}

oserr_t
MemorySpaceMap(
        _In_  MemorySpace_t*                memorySpace,
        _In_  struct MemorySpaceMapOptions* options,
        _Out_ vaddr_t*                      mappingOut)
{
    struct MSAllocation* allocation;
    int                  pageCount;
    vaddr_t              virtualBase;
    oserr_t              oserr;

    if (memorySpace == NULL || options == NULL) {
        return OS_EINVALPARAMS;
    }
    TRACE("MemorySpaceMap(len=%" PRIuIN ", attribs=0x%x, placement=0x%x)",
          length, memoryFlags, placementFlags);

    // Calculate the number of pages we want to affect.
    pageCount = DIVUP(options->Length, GetMemorySpacePageSize());

    // Existing allocation we are touching?
    if (__VMTYPE(options->PlacementFlags) == MAPPING_VIRTUAL_FIXED) {
        allocation = MSAllocationLookup(memorySpace->Context, options->VirtualStart);
        if (allocation) {
            // OK existing allocation, there is a good chance we want to commit a part (or all) of it,
            // if MAPPING_COMMIT is set. To change protection flags of a mapping, MemorySpaceChangeProtection
            // must be used.
            if (options->Flags & MAPPING_COMMIT) {
                virtualBase = allocation->Address;
                goto getPhysicalPages;
            }
            return OS_ENOTSUPPORTED;
        }

        // Reserve one of the pages for the STACK mappings if the mappings
        // are pre-provided.
        if (options->Flags & MAPPING_STACK) {
            pageCount--;
        }
    }

    // We must allocate a virtual region first, because once we start filling in
    // physical pages it becomes impossible to properly track what we filled in our
    // self and what was provided. It is a lot easier to clean up a single virtual
    // mapping.
    oserr = __AllocateVirtualMemory(memorySpace, options, &virtualBase);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Update the mapping pointer supplied before continuing, so we don't do this
    // multiple places. No corrections will be done to virtualBase from here.
    if (options->Flags & MAPPING_STACK) {
        *mappingOut = (virtualBase + (pageCount * GetMemorySpacePageSize()));
    } else {
        *mappingOut = virtualBase;
    }

    // If we are trying to reserve memory through this call, redirect it to the
    // dedicated reservation method. 
    if (!(options->Flags & MAPPING_COMMIT)) {
        oserr = __ReserveMapping(memorySpace, virtualBase, options->Flags, pageCount);
        if (oserr != OS_EOK) {
            goto cleanup;
        }
        return oserr;
    }

    // If we are doing a contigious physical mapping, then we should use another underlying call
    if (__PMTYPE(options->PlacementFlags) == MAPPING_PHYSICAL_CONTIGUOUS) {
        oserr = __ContiguousMapping(
                memorySpace,
                virtualBase,
                options->PhysicalStart,
                pageCount,
                options->Flags
        );
        if (oserr != OS_EOK) {
            goto cleanup;
        }
        return oserr;
    }

    // If physical mappings are not provided in options->Pages, then fill it with mappings.
    // TODO: should we support partially filled pages?
getPhysicalPages:
    if (__PMTYPE(options->PlacementFlags) != MAPPING_PHYSICAL_FIXED) {
        oserr = AllocatePhysicalMemory(options->Mask, pageCount, &options->Pages[0]);
        if (oserr != OS_EOK) {
            ERROR("MemorySpaceMap: cannot allocate physical memory for mapping");
            goto cleanup;
        }
    }

    oserr = __CommitMapping(
            memorySpace,
            virtualBase,
            options->Pages,
            pageCount,
            options->Flags
    );
    if (oserr != OS_EOK) {
        ERROR("MemorySpaceMap: cannot commit allocated mappings");
        goto cleanup;
    }
    return oserr;

cleanup:
    __FreeVirtualMemory(
            memorySpace,
            virtualBase,
            options->Length,
            options->Flags,
            options->PlacementFlags
    );
    return oserr;
}

oserr_t
MemorySpaceCommit(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ uintptr_t*     physicalAddressValues,
        _In_ size_t         size,
        _In_ size_t         pageMask,
        _In_ unsigned int   placementFlags)
{
    int     pageCount = DIVUP(size, GetMemorySpacePageSize());
    int     pagesComitted;
    oserr_t oserr;

    if (!memorySpace || !physicalAddressValues) {
        return OS_EINVALPARAMS;
    }

    if (__PMTYPE(placementFlags) != MAPPING_PHYSICAL_FIXED) {
        oserr = AllocatePhysicalMemory(pageMask, pageCount, &physicalAddressValues[0]);
        if (oserr != OS_EOK) {
            return oserr;
        }
    }

    oserr = ArchMmuCommitVirtualPage(
            memorySpace,
            address,
            &physicalAddressValues[0],
            pageCount,
            &pagesComitted
    );
    if (oserr != OS_EOK) {
        if (__PMTYPE(placementFlags) != MAPPING_PHYSICAL_FIXED) {
            FreePhysicalMemory(pageCount, &physicalAddressValues[0]);
        }
    }
    return oserr;
}

static oserr_t
__GetAndVerifyPhysicalMapping(
        _In_  MemorySpace_t* sourceSpace,
        _In_  vaddr_t        address,
        _In_  int            pageCount,
        _Out_ uintptr_t**    physicalAddressesOut,
        _Out_ int*           pagesRetrievedOut)
{
    uintptr_t* physicalAddresses;
    oserr_t    oserr;
    int        pagesRetrieved;
    int        i;
    TRACE("__GetAndVerifyPhysicalMapping(address=0x%" PRIxIN ", pageCount=%i",
          address, pageCount);

    // Get the physical mappings first and verify them. They _MUST_ be committed in order for us to clone
    // the mapping, otherwise the mapping can get out of sync. And we do not want that.
    physicalAddresses = (uintptr_t*)kmalloc(pageCount * sizeof(uintptr_t));
    if (!physicalAddresses) {
        return OS_EOOM;
    }

    // Allocate a temporary array to store physical mappings
    oserr = ArchMmuVirtualToPhysical(sourceSpace, address, pageCount, &physicalAddresses[0], &pagesRetrieved);
    if (oserr != OS_EOK) {
        kfree(physicalAddresses);
        return oserr;
    }

    // Verify mappings, we don't support partial clones. When cloning a mapping the
    // entire mapping must be comitted.
    TRACE("__GetAndVerifyPhysicalMapping pagesRetrieved=%i", pagesRetrieved);
    for (i = 0; i < pagesRetrieved; i++) {
        if (!physicalAddresses[i]) {
            ERROR("__GetAndVerifyPhysicalMapping offset %i was 0 [0x%" PRIxIN "]",
                  i, address + (i * GetMemorySpacePageSize()));
            oserr = OS_ENOTSUPPORTED;
        }
    }

    *physicalAddressesOut = physicalAddresses;
    *pagesRetrievedOut = pagesRetrieved;
    return oserr;
}

oserr_t
MemorySpaceCloneMapping(
        _In_        MemorySpace_t* sourceSpace,
        _In_        MemorySpace_t* destinationSpace,
        _In_        vaddr_t        sourceAddress,
        _InOut_Opt_ vaddr_t*       destinationAddress,
        _In_        size_t         length,
        _In_        unsigned int   memoryFlags,
        _In_        unsigned int   placementFlags)
{
    struct MSAllocation* sourceAllocation = NULL;
    vaddr_t              virtualBase;
    int                  pageCount = DIVUP(length, GetMemorySpacePageSize());
    int                  pagesRetrieved;
    uintptr_t*           pages = NULL;
    oserr_t              oserr;

    TRACE("MemorySpaceCloneMapping(sourceSpace=0x%" PRIxIN ", destinationSpace=0x%" PRIxIN
          ", sourceAddress=0x%" PRIxIN ", length=0x%" PRIxIN ", memoryFlags=0x%x)",
          sourceSpace, destinationSpace, sourceAddress, length, memoryFlags);

    if (!sourceSpace || !destinationSpace || !destinationAddress) {
        return OS_EINVALPARAMS;
    }

    // Increase reference of the source allocation first, to "acquire" it. We are not sure
    // that an allocation exists, especially if the allocation is made in non process-memory, so
    // take into account that sourceAllocation may be NULL
    sourceAllocation = MSAllocationAcquire(sourceSpace->Context, sourceAddress);
    oserr = __GetAndVerifyPhysicalMapping(
            sourceSpace,
            sourceAddress,
            pageCount,
            &pages,
            &pagesRetrieved
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    oserr = __AllocateVirtualMemory(
            destinationSpace,
            &(struct MemorySpaceMapOptions) {
                .VirtualStart = *destinationAddress,
                .Length = length,
                .Flags = memoryFlags,
                .PlacementFlags = placementFlags
            },
            &virtualBase
    );
    if (oserr != OS_EOK) {
        goto exit;
    }

    *destinationAddress = virtualBase;

    // bind the source and destination allocation together
    if (sourceAllocation) {
        MSAllocationLink(destinationSpace->Context, virtualBase, sourceAllocation);
    }

    oserr = __CommitMapping(
            destinationSpace,
            virtualBase,
            &pages[0],
            pagesRetrieved,
            memoryFlags | MAPPING_PERSISTENT | MAPPING_COMMIT
    );

exit:
    if (pages) {
        kfree(pages);
    }

    if (oserr != OS_EOK) {
        if (sourceAllocation) {
            struct MSAllocation* originalMapping;
            (void)MSAllocationFree(
                    destinationSpace->Context,
                    virtualBase,
                    length,
                    &originalMapping
            );
        }
    }

    TRACE("MemorySpaceCloneMapping returns=%u", oserr);
    return oserr;
}
