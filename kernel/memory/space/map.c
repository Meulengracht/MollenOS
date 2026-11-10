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
#include "../shm/private.h"

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

    // Fixed stack regions must cover at least two pages, including the guard page.
    if ((options->Flags & MAPPING_STACK) && options->Length <= GetMemorySpacePageSize()) {
        return OS_EINVALPARAMS;
    }

    // Align the provided value on a page-boundary
    *baseAddressOut = options->VirtualStart & ~(GetMemorySpacePageSize() - 1);

    // Skip the guard page when returning a stack base.
    if (options->Flags & MAPPING_STACK) {
        *baseAddressOut += GetMemorySpacePageSize();
    }
    return OS_EOK;
}

static oserr_t
__AllocateVirtualMemory(
    _In_  MemorySpace_t*                memorySpace,
    _In_  struct MemorySpaceMapOptions* options,
    _Out_ vaddr_t*                      baseAddressOut)
{
    vaddr_t virtualBase;
    size_t  length = options->Length;

    // Reserve an extra virtual page at the start of a stack.
    // This guard page stays unmapped.
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
            virtualBase = MSMappingAllocate(memorySpace, options, length);
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

    // Skip the guard page for stack mappings.
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
    // Include the guard page when freeing a stack's virtual region.
    if (memoryFlags & MAPPING_STACK) {
        address -= GetMemorySpacePageSize();
        size += GetMemorySpacePageSize();
    }

    _CRT_UNUSED(size);
    switch (__VMTYPE(placementFlags)) {
        case MAPPING_VIRTUAL_PROCESS: {
            struct MSContext* context = __MSContextGet(memorySpace);
            DynamicMemoryPoolFree(&context->Heap, address);
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

static oserr_t
__MapSHMPages(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ paddr_t*       pages,
    _In_ int            pageCount,
    _In_ unsigned int   flags)
{
    int          first = 0;
    int          end;
    int          updated;
    bool         committed;
    unsigned int attributes;
    oserr_t      status;

    // Missing backing pages remain reservations until SHM commits them.
    // Group adjacent pages by commit state so fully populated buffers use one MMU call.
    while (first < pageCount) {
        committed = pages[first] != 0;
        end = first + 1;
        while (end < pageCount && (pages[end] != 0) == committed) {
            end++;
        }
        attributes = flags & ~MAPPING_COMMIT;
        if (committed) {
            attributes |= MAPPING_COMMIT;
        }
        status = ArchMmuSetVirtualPages(
            memorySpace,
            address + (size_t)first * GetMemorySpacePageSize(),
            &pages[first],
            end - first,
            attributes,
            &updated
        );
        if (status != OS_EOK) {
            return status;
        }
        first = end;
    }
    return OS_EOK;
}

oserr_t
MSMap(
        _In_  MemorySpace_t*                memorySpace,
        _In_  struct MemorySpaceMapOptions* options,
        _Out_ vaddr_t*                      mappingOut)
{
    struct MSAllocation* allocation;
    int                  pageCount;
    vaddr_t              virtualBase;
    oserr_t              oserr;
    oserr_t              cleanupStatus;
    bool                 allowFreedPages;

    if (memorySpace == NULL || options == NULL) {
        return OS_EINVALPARAMS;
    }
    TRACE("MemorySpaceMap(len=%" PRIuIN ", attribs=0x%x, placement=0x%x)",
          length, memoryFlags, placementFlags);

    if (options->SHMView != NULL) {
        if (__VMTYPE(options->PlacementFlags) != MAPPING_VIRTUAL_PROCESS ||
            __PMTYPE(options->PlacementFlags) != MAPPING_PHYSICAL_FIXED) {
            return OS_ENOTSUPPORTED;
        }
        oserr = SHMViewValidateMapping(options->SHMView, options->SHMOffset,
                                      options->Length, options->Flags);
        if (oserr != OS_EOK) {
            return oserr;
        }
        options->Flags |= MAPPING_PERSISTENT;
    }
    if (__VMTYPE(options->PlacementFlags) == MAPPING_VIRTUAL_FIXED) {
        allowFreedPages = false;
        oserr = MSAllocationFindSHM(
            __MSContextGet(memorySpace),
            options->VirtualStart,
            options->Length,
            allowFreedPages,
            &allocation
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        if (allocation != NULL) {
            return OS_EPERMISSIONS;
        }
    }

    // Calculate the number of pages in the mapping.
    pageCount = DIVUP(options->Length, GetMemorySpacePageSize());

    // Check for an existing allocation at the fixed address.
    if (__VMTYPE(options->PlacementFlags) == MAPPING_VIRTUAL_FIXED) {
        allocation = MSAllocationLookup(__MSContextGet(memorySpace), options->VirtualStart);
        if (allocation) {
            // Use MAPPING_COMMIT to commit part or all of an existing allocation.
            // Protection changes must use MemorySpaceChangeProtection.
            if (options->Flags & MAPPING_COMMIT) {
                virtualBase = allocation->Address;
                goto getPhysicalPages;
            }
            return OS_ENOTSUPPORTED;
        }

        // Fixed stack regions include a guard page. Exclude it from pageCount.
        if (options->Flags & MAPPING_STACK) {
            pageCount--;
        }
    }

    // Allocate virtual memory first so cleanup can release a single mapping
    // without distinguishing supplied pages from pages allocated here.
    oserr = __AllocateVirtualMemory(memorySpace, options, &virtualBase);
    if (oserr != OS_EOK) {
        return oserr;
    }

    // Set mappingOut before mapping pages. virtualBase will not change after this point.
    if (options->Flags & MAPPING_STACK) {
        *mappingOut = (virtualBase + (pageCount * GetMemorySpacePageSize()));
    } else {
        *mappingOut = virtualBase;
    }

    // Use the reservation path when MAPPING_COMMIT is clear.
    if (!(options->Flags & MAPPING_COMMIT)) {
        oserr = __ReserveMapping(memorySpace, virtualBase, options->Flags, pageCount);
        if (oserr != OS_EOK) {
            goto cleanup;
        }
        return oserr;
    }

    // Contiguous physical mappings use a separate MMU call.
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

    // Allocate physical pages in options->Pages unless MAPPING_PHYSICAL_FIXED is set.
    // TODO: should we support partially filled pages?
getPhysicalPages:
    if (__PMTYPE(options->PlacementFlags) != MAPPING_PHYSICAL_FIXED) {
        oserr = AllocatePhysicalMemory(options->Mask, pageCount, &options->Pages[0]);
        if (oserr != OS_EOK) {
            ERROR("MemorySpaceMap: cannot allocate physical memory for mapping");
            goto cleanup;
        }
    }

    if (options->SHMView != NULL) {
        oserr = __MapSHMPages(memorySpace, virtualBase, options->Pages, pageCount, options->Flags);
    } else {
        oserr = __CommitMapping(memorySpace, virtualBase, options->Pages, pageCount, options->Flags);
    }
    if (oserr != OS_EOK) {
        ERROR("MemorySpaceMap: cannot commit allocated mappings");
        goto cleanup;
    }
    return oserr;

cleanup:
    if (options->SHMView != NULL) {
        cleanupStatus = MSMappingRollback(memorySpace, virtualBase, options->Length);
        if (cleanupStatus != OS_EOK) {
            oserr = cleanupStatus;
        }
        if (mappingOut != NULL) {
            *mappingOut = 0;
        }
        return oserr;
    }
    __FreeVirtualMemory(
            memorySpace,
            virtualBase,
            options->Length,
            options->Flags,
            options->PlacementFlags
    );
    if (mappingOut) {
        *mappingOut = 0;
    }
    return oserr;
}

oserr_t
MemorySpaceMap(
    _In_  MemorySpace_t*                memorySpace,
    _In_  struct MemorySpaceMapOptions* options,
    _Out_ vaddr_t*                      mappingOut)
{
    struct MSContext* context;
    oserr_t           status;

    if (memorySpace == NULL || options == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }
    context = (options->Flags & MAPPING_USERSPACE) ? __MSContextGet(memorySpace) : NULL;
    if (context != NULL) {
        MutexLock(&context->UserCopyLock);
    }
    status = MSMap(memorySpace, options, mappingOut);
    if (context != NULL) {
        MutexUnlock(&context->UserCopyLock);
    }
    return status;
}

static oserr_t
__Commit(
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

oserr_t
MemorySpaceCommit(
    _In_ MemorySpace_t* memorySpace,
    _In_ vaddr_t        address,
    _In_ uintptr_t*     physicalAddressValues,
    _In_ size_t         size,
    _In_ size_t         pageMask,
    _In_ unsigned int   placementFlags)
{
    struct MSContext*    context;
    struct MSAllocation* allocation;
    struct SHMBuffer*    buffer;
    size_t               pageSize = GetMemorySpacePageSize();
    size_t               index;
    size_t               count;
    size_t               i;
    oserr_t              status;
    bool                 allowFreedPages;

    if (memorySpace == NULL || physicalAddressValues == NULL) {
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
        size,
        allowFreedPages,
        &allocation
    );
    if (status != OS_EOK) {
        goto exit;
    }
    if (allocation != NULL) {
        if (address % pageSize != 0 || size > SIZE_MAX - (pageSize - 1) ||
            __PMTYPE(placementFlags) != MAPPING_PHYSICAL_FIXED) {
            status = OS_EINVALPARAMS;
            goto exit;
        }
        buffer = allocation->SHM.View->Buffer;
        index = (buffer->Offset + allocation->SHM.Offset + address - allocation->Address) / pageSize;
        count = DIVUP(size, pageSize);
        for (i = 0; i < count; i++) {
            if (physicalAddressValues[i] != buffer->Pages[index + i]) {
                status = OS_EPERMISSIONS;
                goto exit;
            }
        }
    }
    status = __Commit(memorySpace, address, physicalAddressValues, size, pageMask, placementFlags);
exit:
    if (context != NULL) {
        MutexUnlock(&context->UserCopyLock);
    }
    return status;
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

    // The source mapping must be fully committed before cloning, otherwise the
    // cloned mapping can get out of sync.
    physicalAddresses = (uintptr_t*)kmalloc(pageCount * sizeof(uintptr_t));
    if (!physicalAddresses) {
        return OS_EOOM;
    }

    // Read the physical addresses into the array.
    oserr = ArchMmuVirtualToPhysical(sourceSpace, address, pageCount, &physicalAddresses[0], &pagesRetrieved);
    if (oserr != OS_EOK) {
        kfree(physicalAddresses);
        return oserr;
    }

    // Partial clones are not supported. Every source page must be committed.
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

static oserr_t
__CloneMapping(
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
    size_t               pageSize = GetMemorySpacePageSize();
    size_t               pageOffset = sourceAddress % pageSize;
    struct MSAllocation* restricted;
    struct MemorySpaceMapOptions options;
    bool                 allowFreedPages;

    TRACE("MemorySpaceCloneMapping(sourceSpace=0x%" PRIxIN ", destinationSpace=0x%" PRIxIN
          ", sourceAddress=0x%" PRIxIN ", length=0x%" PRIxIN ", memoryFlags=0x%x)",
          sourceSpace, destinationSpace, sourceAddress, length, memoryFlags);

    if (!sourceSpace || !destinationSpace || !destinationAddress) {
        return OS_EINVALPARAMS;
    }

    allowFreedPages = false;
    oserr = MSAllocationFindSHM(
        __MSContextGet(sourceSpace),
        sourceAddress,
        length,
        allowFreedPages,
        &restricted
    );
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    if (restricted != NULL) {
        oserr = MSMappingCloneOptions(
            restricted,
            sourceAddress,
            length,
            memoryFlags,
            placementFlags,
            &options
        );
        if (oserr != OS_EOK) {
            return oserr;
        }
        pageCount = (int)(options.Length / pageSize);
        oserr = __GetAndVerifyPhysicalMapping(sourceSpace, sourceAddress - pageOffset,
                                              pageCount, &pages, &pagesRetrieved);
        if (oserr != OS_EOK) {
            kfree(pages);
            return oserr;
        }
        if (pagesRetrieved != pageCount) {
            kfree(pages);
            return OS_ENOTSUPPORTED;
        }
        options.Pages = pages;
        oserr = MSMap(destinationSpace, &options, &virtualBase);
        kfree(pages);
        if (oserr == OS_EOK) {
            *destinationAddress = virtualBase + pageOffset;
        }
        return oserr;
    }

    // Increase the source allocation's reference count before cloning. Allocations
    // outside process memory may be untracked, so sourceAllocation can be NULL.
    sourceAllocation = MSAllocationAcquire(__MSContextGet(sourceSpace), sourceAddress);
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

    // Link the source and destination allocations.
    if (sourceAllocation) {
        MSAllocationLink(__MSContextGet(destinationSpace), virtualBase, sourceAllocation);
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
                __MSContextGet(destinationSpace),
                virtualBase,
                length,
                &originalMapping
            );
        }
    }

    TRACE("MemorySpaceCloneMapping returns=%u", oserr);
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
    struct MSContext* first;
    struct MSContext* second;
    struct MSContext* temporary;
    oserr_t           status;

    if (sourceSpace == NULL || destinationSpace == NULL || destinationAddress == NULL) {
        return OS_EINVALPARAMS;
    }
    
    first = __MSContextGet(sourceSpace);
    second = __MSContextGet(destinationSpace);
    if (first == second) {
        second = NULL;
    } else if ((uintptr_t)first > (uintptr_t)second) {
        temporary = first;
        first = second;
        second = temporary;
    }
    
    // Lock contexts in address order when a kernel caller clones across two groups.
    if (first != NULL) {
        MutexLock(&first->UserCopyLock);
    }
    if (second != NULL) {
        MutexLock(&second->UserCopyLock);
    }
    
    status = __CloneMapping(
        sourceSpace,
        destinationSpace,
        sourceAddress,
        destinationAddress,
        length,
        memoryFlags,
        placementFlags
    );
    if (second != NULL) {
        MutexUnlock(&second->UserCopyLock);
    }
    if (first != NULL) {
        MutexUnlock(&first->UserCopyLock);
    }
    return status;
}
