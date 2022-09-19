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
#include <assert.h>
#include <component/cpu.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <machine.h>
#include <memoryspace.h>
#include <mutex.h>
#include <string.h>
#include <threading.h>

struct MemorySpaceAllocation {
    element_t                     Header;
    MemorySpace_t*                MemorySpace;
    vaddr_t                       Address;
    size_t                        Length;
    unsigned int                  Flags;
    int                           References;
    struct MemorySpaceAllocation* CloneOf;
};

struct MemorySynchronizationObject {
    _Atomic(int) CallsCompleted;
    uuid_t       MemorySpaceHandle;
    uintptr_t    Address;
    size_t       Length;
};

// one per thread group [process]
typedef struct MemorySpaceContext {
    DynamicMemoryPool_t Heap;
    list_t              Allocations;
    uintptr_t           SignalHandler;
    Mutex_t             SyncObject;
} MemorySpaceContext_t;

static void DestroyMemorySpace(void* resource);

static void
__MemorySyncCallback(
    _In_ void* context)
{
    struct MemorySynchronizationObject* object  = (struct MemorySynchronizationObject*)context;
    MemorySpace_t*                      current = GetCurrentMemorySpace();
    uuid_t                              currentHandle = GetCurrentMemorySpaceHandle();

    // Make sure the current address space is matching
    // If NULL => everyone must update
    // If it matches our parent, we must update
    // If it matches us, we must update
    if (object->MemorySpaceHandle == UUID_INVALID || current->ParentHandle == object->MemorySpaceHandle ||
        currentHandle == object->MemorySpaceHandle) {
        CpuInvalidateMemoryCache((void*)object->Address, object->Length);
    }
    atomic_fetch_add(&object->CallsCompleted, 1);
}

static void
__SyncMemoryRegion(
        _In_ MemorySpace_t* memorySpace,
        _In_ uintptr_t      address,
        _In_ size_t         size)
{
    // We can easily allocate this object on the stack as the stack is globally
    // visible to all kernel code. This spares us allocation on heap
    struct MemorySynchronizationObject Object = {
        .Address        = address,
        .Length         = size,
        .CallsCompleted = 0
    };
    
    int     numberOfCores;
    int     numberOfActiveCores;
    clock_t interruptedAt;
    size_t  timeout = 1000;

    // Skip this entire step if there is no multiple cores active
    numberOfActiveCores = atomic_load(&GetMachine()->NumberOfActiveCores);
    if (numberOfActiveCores <= 1) {
        return;
    }

    // Check for global address, in that case invalidate all cores
    if (StaticMemoryPoolContains(&GetMachine()->GlobalAccessMemory, address)) {
        Object.MemorySpaceHandle = UUID_INVALID; // Everyone must update
    }
    else {
        if (memorySpace->ParentHandle == UUID_INVALID) {
            Object.MemorySpaceHandle = GetCurrentMemorySpaceHandle(); // Children of us must update
        }
        else {
            Object.MemorySpaceHandle = memorySpace->ParentHandle; // Parent and siblings!
        }
    }

    numberOfCores = ProcessorMessageSend(1, CpuFunctionCustom, __MemorySyncCallback, &Object, 1);
    while (atomic_load(&Object.CallsCompleted) != numberOfCores && timeout > 0) {
        SchedulerSleep(5 * NSEC_PER_MSEC, &interruptedAt);
        timeout -= 5;
    }
    
    if (!timeout) {
        ERROR("[memory] [sync] timeout trying to synchronize with cores actual %i != target %i",
              atomic_load(&Object.CallsCompleted), numberOfCores);
    }
}

static oserr_t
__CreateContext(
        _In_ MemorySpace_t* memorySpace)
{
    MemorySpaceContext_t* context = (MemorySpaceContext_t*)kmalloc(sizeof(MemorySpaceContext_t));
    if (!context) {
        return OsOutOfMemory;
    }

    MutexConstruct(&context->SyncObject, MUTEX_FLAG_PLAIN);
    DynamicMemoryPoolConstruct(&context->Heap, GetMachine()->MemoryMap.UserHeap.Start,
                               GetMachine()->MemoryMap.UserHeap.Length, GetMachine()->MemoryGranularity);
    list_construct(&context->Allocations);
    context->SignalHandler = 0;

    memorySpace->Context = context;
    return OsOK;
}

static void
__CleanupMemoryAllocation(
        _In_ element_t* element,
        _In_ void*      context)
{
    struct MemorySpaceAllocation* allocation = element->value;
    MemorySpace_t*                memorySpace = context;

    DynamicMemoryPoolFree(&memorySpace->Context->Heap, allocation->Address);
    kfree(allocation);
}

static void
__DestroyContext(
        _In_ MemorySpace_t* memorySpace)
{
    if (!memorySpace->Context) {
        return;
    }

    MutexDestruct(&memorySpace->Context->SyncObject);
    list_clear(&memorySpace->Context->Allocations, __CleanupMemoryAllocation, memorySpace);
    DynamicMemoryPoolDestroy(&memorySpace->Context->Heap);
    kfree(memorySpace->Context);
}

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

static MemorySpace_t*
__NewMemorySpace(
        _In_ unsigned int flags)
{
    uintptr_t      threadRegionStart;
    size_t         threadRegionSize;
    MemorySpace_t* memorySpace;
    TRACE("__NewMemorySpace(flags=0x%x)", flags);

    memorySpace = (MemorySpace_t*)kmalloc(sizeof(MemorySpace_t));
    if (!memorySpace) {
        return NULL;
    }
    memset((void*)memorySpace, 0, sizeof(MemorySpace_t));

    threadRegionStart = GetMachine()->MemoryMap.ThreadLocal.Start;
    threadRegionSize  = GetMachine()->MemoryMap.ThreadLocal.Length + 1;

    memorySpace->Flags        = flags;
    memorySpace->ParentHandle = UUID_INVALID;
    DynamicMemoryPoolConstruct(
            &memorySpace->ThreadMemory,
            threadRegionStart,
            threadRegionSize,
            GetMemorySpacePageSize()
    );
    return memorySpace;
}

oserr_t
CreateMemorySpace(
        _In_  unsigned int flags,
        _Out_ uuid_t*      handleOut)
{
    MemorySpace_t* memorySpace;
    oserr_t     osStatus;
    TRACE("CreateMemorySpace(flags=0x%x)", flags);

    memorySpace = __NewMemorySpace(flags);
    if (!memorySpace) {
        return OsOutOfMemory;
    }

    // We must handle two cases here, either we inherit the kernels address-space, or we are
    // inheritting/creating a new userspace address-space. If we are inheritting the kernel
    // address-space, we clone it as we still need TLS data-areas in each kernel thread.
    if (flags == MEMORY_SPACE_INHERIT) {
        // When cloning kernel memory spaces, we initially just copy the platform block
        // for the new memory space, and then let the Platfrom call correct anything.
        if (GetCurrentMemorySpace() != NULL) {
            memcpy(
                    &memorySpace->PlatfromData,
                    &GetCurrentMemorySpace()->PlatfromData,
                    sizeof(PlatformMemoryBlock_t)
            );
        }
        
        // It doesn't matter which parent we take, they all map the exact same kernel segments
        // so just pass in the current memory space
        osStatus = MmuCloneVirtualSpace(
                GetCurrentMemorySpace(),
                memorySpace,
                (flags & MEMORY_SPACE_INHERIT) ? 1 : 0
        );
        if (osStatus != OsOK) {
            return osStatus;
        }

        *handleOut = GetCurrentMemorySpaceHandle();
    }
    else if (flags & MEMORY_SPACE_APPLICATION) {
        MemorySpace_t* parent = NULL;

        // Parent must be the uppermost instance of the address-space
        // of the process. Only to the point of not having kernel as parent
        if (flags & MEMORY_SPACE_INHERIT) {
            parent = GetCurrentMemorySpace();
            if (parent != GetDomainMemorySpace()) {
                if (parent->ParentHandle != UUID_INVALID) {
                    memorySpace->ParentHandle = parent->ParentHandle;
                    memorySpace->Context      = parent->Context;
                    parent = (MemorySpace_t*)LookupHandleOfType(
                            parent->ParentHandle, HandleTypeMemorySpace);
                }
                else {
                    memorySpace->ParentHandle = GetCurrentMemorySpaceHandle();
                    memorySpace->Context      = parent->Context;
                }

                // Add a reference and copy data
                AcquireHandle(memorySpace->ParentHandle, NULL);
                memcpy(&memorySpace->PlatfromData, &parent->PlatfromData, sizeof(PlatformMemoryBlock_t));
            }
            else {
                parent = NULL;
            }
        }
        
        // If we are root, create the memory bitmaps
        if (memorySpace->ParentHandle == UUID_INVALID) {
            __CreateContext(memorySpace);
        }

        osStatus = MmuCloneVirtualSpace(parent, memorySpace, (flags & MEMORY_SPACE_INHERIT) ? 1 : 0);
        if (osStatus != OsOK) {
            return osStatus;
        }

        *handleOut = CreateHandle(HandleTypeMemorySpace, DestroyMemorySpace, memorySpace);
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in CreateMemorySpace 0x%" PRIxIN "", flags);
    }
    return OsOK;
}

static void
DestroyMemorySpace(
        _In_ void* resource)
{
    MemorySpace_t* memorySpace = (MemorySpace_t*)resource;
    if (!memorySpace) {
        return;
    }

    if (memorySpace->Flags & MEMORY_SPACE_APPLICATION) {
        DynamicMemoryPoolDestroy(&memorySpace->ThreadMemory);
        MmuDestroyVirtualSpace(memorySpace);
    }
    if (memorySpace->ParentHandle == UUID_INVALID) {
        __DestroyContext(memorySpace);
    }
    if (memorySpace->ParentHandle != UUID_INVALID) {
        DestroyHandle(memorySpace->ParentHandle);
    }
    kfree(memorySpace);
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
    }
    else {
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
    }
    else {
        return ThreadMemorySpaceHandle(currentThread);
    }
}

MemorySpace_t*
GetDomainMemorySpace(void)
{
    return (GetCurrentDomain() != NULL) ? &GetCurrentDomain()->SystemSpace : &GetMachine()->SystemSpace;
}

oserr_t
AreMemorySpacesRelated(
        _In_ MemorySpace_t* Space1,
        _In_ MemorySpace_t* Space2)
{
    return (Space1->Context == Space2->Context) ? OsOK : OsError;
}

static oserr_t
__CreateAllocation(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         length,
        _In_ unsigned int   flags)
{
    struct MemorySpaceAllocation* allocation;
    oserr_t                    osStatus = OsOK;

    TRACE("__CreateAllocation(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ", flags=0x%x)",
          memorySpace, address, length, flags);

    // We only support allocation tracking for spaces with context
    if (!memorySpace->Context) {
        goto exit;
    }

    allocation = kmalloc(sizeof(struct MemorySpaceAllocation));
    if (!allocation) {
        osStatus = OsOutOfMemory;
        goto exit;
    }

    ELEMENT_INIT(&allocation->Header, 0, allocation);
    allocation->MemorySpace = memorySpace;
    allocation->Address     = address;
    allocation->Length      = length;
    allocation->Flags       = flags;
    allocation->References  = 1;
    allocation->CloneOf     = NULL;

    MutexLock(&memorySpace->Context->SyncObject);
    list_append(&memorySpace->Context->Allocations, &allocation->Header);
    MutexUnlock(&memorySpace->Context->SyncObject);

exit:
    TRACE("__CreateAllocation returns=%u", osStatus);
    return osStatus;
}

static vaddr_t
__AllocateVirtualMemory(
        _In_ MemorySpace_t* memorySpace,
        _In_ uintptr_t*     virtualAddress,
        _In_ size_t         size,
        _In_ unsigned int   memoryFlags,
        _In_ unsigned int   placementFlags)
{
    vaddr_t      virtualBase  = 0;
    unsigned int virtualFlags = placementFlags & MAPPING_VIRTUAL_MASK;

    // Is this a stack allocation? Then we need to allocate another page
    // for the allocation, which will be the first page in the segment of memory.
    // this segment will be unmapped, but allocated in virtual memory.
    if (memoryFlags & MAPPING_GUARDPAGE) {
        size += GetMemorySpacePageSize();
    }

    switch (virtualFlags) {
        case MAPPING_VIRTUAL_FIXED: {
            if (virtualAddress) {
                virtualBase = *virtualAddress;
            }
        } break;

        case MAPPING_VIRTUAL_PROCESS: {
            assert(memorySpace->Context != NULL);
            virtualBase = DynamicMemoryPoolAllocate(&memorySpace->Context->Heap, size);
            if (virtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%" PRIxIN " (heap)", size);
            }
            else {
                // We only track user allocations, not kernel allocations. If we wanted to track ALL allocations
                // then we would have to guard against eternal loops as-well as the __CreateAllocation actually calls
                // kmalloc
                oserr_t osStatus = __CreateAllocation(memorySpace, virtualBase, size, memoryFlags);
                if (osStatus != OsOK) {
                    ERROR("__AllocateVirtualMemory failed to register allocation");
                    DynamicMemoryPoolFree(&memorySpace->Context->Heap, size);
                    virtualBase = 0;
                }
            }
        } break;

        case MAPPING_VIRTUAL_THREAD: {
            assert((memorySpace->Flags & MEMORY_SPACE_APPLICATION) != 0);
            virtualBase = DynamicMemoryPoolAllocate(&memorySpace->ThreadMemory, size);
            if (virtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%" PRIxIN " (tls)", size);
            }
        } break;

        case MAPPING_VIRTUAL_GLOBAL: {
            virtualBase = StaticMemoryPoolAllocate(&GetMachine()->GlobalAccessMemory, size);
            if (virtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%" PRIxIN " (ga-memory)", size);
            }
        } break;

        default: {
            FATAL(FATAL_SCOPE_KERNEL, "Failed to allocate virtual memory for flags: 0x%" PRIxIN "", virtualFlags);
        } break;
    }

    // Now fixup the allocated address if the allocation was a stack
    if (virtualBase) {
        if (memoryFlags & MAPPING_GUARDPAGE) {
            virtualBase += GetMemorySpacePageSize();
        }

        if (virtualAddress) {
            *virtualAddress = virtualBase;
        }
    }
    return virtualBase;
}

oserr_t
MemorySpaceMap(
        _In_    MemorySpace_t* memorySpace,
        _InOut_ vaddr_t*       address,
        _InOut_ uintptr_t*     physicalAddressValues,
        _In_    size_t         length,
        _In_    size_t         pageMask,
        _In_    unsigned int   memoryFlags,
        _In_    unsigned int   placementFlags)
{
    int        pageCount = DIVUP(length, GetMemorySpacePageSize());
    int        pagesUpdated;
    vaddr_t    virtualBase;
    oserr_t osStatus;
    TRACE("MemorySpaceMap(len=%" PRIuIN ", attribs=0x%x, placement=0x%x)",
          length, memoryFlags, placementFlags);
    
    // If we are trying to reserve memory through this call, redirect it to the
    // dedicated reservation method. 
    if (!(memoryFlags & MAPPING_COMMIT)) {
        return MemorySpaceMapReserved(memorySpace, address, length, memoryFlags, placementFlags);
    }
    
    assert(memorySpace != NULL);
    assert(physicalAddressValues != NULL);
    assert(placementFlags != 0);
    
    // In case the mappings are provided, we would like to force the COMMIT flag.
    if (placementFlags & MAPPING_PHYSICAL_FIXED) {
        memoryFlags |= MAPPING_COMMIT;
    }
    else if (memoryFlags & MAPPING_COMMIT) {
        osStatus = AllocatePhysicalMemory(pageMask, pageCount, &physicalAddressValues[0]);
        if (osStatus != OsOK) {
            ERROR("MemorySpaceMap failed to allocate physical memory for mapping");
            return osStatus;
        }
    }
    
    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    virtualBase = __AllocateVirtualMemory(memorySpace, address, length, memoryFlags, placementFlags);
    if (!virtualBase) {
        // Cleanup physical mappings
        // TODO
        ERROR("MemorySpaceMap implement cleanup of phys virt");
        return OsInvalidParameters;
    }

    osStatus = ArchMmuSetVirtualPages(
            memorySpace,
            virtualBase,
            physicalAddressValues,
            pageCount,
            memoryFlags,
            &pagesUpdated
    );
    if (osStatus != OsOK) {
        // Handle cleanup of the pages not mapped
        // TODO
        ERROR("MemorySpaceMap implement cleanup of phys/virt");
    }
    return osStatus;
}

oserr_t
MemorySpaceMapContiguous(
        _In_    MemorySpace_t* MemorySpace,
        _InOut_ vaddr_t*       Address,
        _In_    uintptr_t      PhysicalStartAddress,
        _In_    size_t         Length,
        _In_    unsigned int   MemoryFlags,
        _In_    unsigned int   PlacementFlags)
{
    int        PageCount = DIVUP(Length, GetMemorySpacePageSize());
    int        PagesUpdated;
    vaddr_t    VirtualBase;
    oserr_t Status;
    
    TRACE("[memory_map_contiguous] %u, 0x%x, 0x%x", 
        LODWORD(Length), MemoryFlags, PlacementFlags);
    
    assert(MemorySpace != NULL);
    assert(PlacementFlags != 0);
    
    // COMMIT must be set when mapping contiguous physical ranges
    MemoryFlags |= MAPPING_COMMIT;
    
    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    VirtualBase = __AllocateVirtualMemory(MemorySpace, Address, Length, MemoryFlags, PlacementFlags);
    if (!VirtualBase) {
        return OsInvalidParameters;
    }
    
    Status = ArchMmuSetContiguousVirtualPages(MemorySpace, VirtualBase, 
        PhysicalStartAddress, PageCount, MemoryFlags, &PagesUpdated);
    if (Status != OsOK) {
        // Handle cleanup of the pages not mapped
        // TODO
        ERROR("[memory_map_contiguous] implement cleanup");
    }
    return Status;
}

oserr_t
MemorySpaceMapReserved(
        _In_    MemorySpace_t* memorySpace,
        _InOut_ vaddr_t*       address,
        _In_    size_t         size,
        _In_    unsigned int   memoryFlags,
        _In_    unsigned int   placementFlags)
{
    int        pageCount = DIVUP(size, GetMemorySpacePageSize());
    int        pagesReserved;
    vaddr_t    virtualBase;
    oserr_t osStatus;
    
    TRACE("[memory_map_reserve] %u, 0x%x, 0x%x", LODWORD(size), memoryFlags, placementFlags);

    if (!memorySpace || !address) {
        return OsInvalidParameters;
    }

    // Clear the COMMIT flag if provided
    memoryFlags &= ~(MAPPING_COMMIT);

    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    virtualBase = __AllocateVirtualMemory(memorySpace, address, size, memoryFlags, placementFlags);
    if (!virtualBase) {
        return OsInvalidParameters;
    }

    osStatus = ArchMmuReserveVirtualPages(memorySpace, virtualBase, pageCount, memoryFlags, &pagesReserved);
    if (osStatus != OsOK) {
        // Handle cleanup of the pages not mapped
        // TODO
        ERROR("[memory_map_reserve] implement cleanup");
    }
    return osStatus;
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
    int        pageCount = DIVUP(size, GetMemorySpacePageSize());
    int        pagesComitted;
    oserr_t osStatus;

    if (!memorySpace || !physicalAddressValues) {
        return OsInvalidParameters;
    }

    if (!(placementFlags & MAPPING_PHYSICAL_FIXED)) {
        osStatus = AllocatePhysicalMemory(pageMask, pageCount, &physicalAddressValues[0]);
        if (osStatus != OsOK) {
            return osStatus;
        }
    }

    osStatus = ArchMmuCommitVirtualPage(memorySpace, address, &physicalAddressValues[0],
                                        pageCount, &pagesComitted);
    if (osStatus != OsOK) {
        ERROR("[memory] [commit] status %u, comitting address 0x%" PRIxIN ", length 0x%" PRIxIN,
              osStatus, address, size);
        if (!(placementFlags & MAPPING_PHYSICAL_FIXED)) {
            FreePhysicalMemory(pageCount, &physicalAddressValues[0]);
        }
    }
    return osStatus;
}

static oserr_t __GetAndVerifyPhysicalMapping(
        _In_  MemorySpace_t* sourceSpace,
        _In_  vaddr_t        address,
        _In_  int            pageCount,
        _Out_ uintptr_t**    physicalAddressesOut,
        _Out_ int*           pagesRetrievedOut)
{
    uintptr_t* physicalAddresses;
    oserr_t osStatus;
    int        pagesRetrieved;
    int        i;
    TRACE("__GetAndVerifyPhysicalMapping(address=0x%" PRIxIN ", pageCount=%i",
          address, pageCount);

    // Get the physical mappings first and verify them. They _MUST_ be committed in order for us to clone
    // the mapping, otherwise the mapping can get out of sync. And we do not want that.
    physicalAddresses = (uintptr_t*)kmalloc(pageCount * sizeof(uintptr_t));
    if (!physicalAddresses) {
        return OsOutOfMemory;
    }

    // Allocate a temporary array to store physical mappings
    osStatus = ArchMmuVirtualToPhysical(sourceSpace, address, pageCount, &physicalAddresses[0], &pagesRetrieved);
    if (osStatus != OsOK) {
        kfree(physicalAddresses);
        return osStatus;
    }

    // Verify mappings
    TRACE("__GetAndVerifyPhysicalMapping pagesRetrieved=%i", pagesRetrieved);
    for (i = 0; i < pagesRetrieved; i++) {
        if (!physicalAddresses[i]) {
            ERROR("__GetAndVerifyPhysicalMapping offset %i was 0 [0x%" PRIxIN "]",
                  i, address + (i * GetMemorySpacePageSize()));
            osStatus = OsError;
        }
    }

    *physicalAddressesOut = physicalAddresses;
    *pagesRetrievedOut = pagesRetrieved;
    return osStatus;
}

static struct MemorySpaceAllocation*
__FindAllocation(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address)
{
    foreach(element, &memorySpace->Context->Allocations) {
        struct MemorySpaceAllocation* allocation = element->value;
        if (address >= allocation->Address && address < (allocation->Address + allocation->Length)) {
            return allocation;
        }
    }
    return NULL;
}

static struct MemorySpaceAllocation*
__AcquireAllocation(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        address)
{
    struct MemorySpaceAllocation* allocation;

    if (!memorySpace->Context) {
        return NULL;
    }

    MutexLock(&memorySpace->Context->SyncObject);
    allocation = __FindAllocation(memorySpace, address);
    if (allocation) {
        allocation->References++;
    }
    MutexUnlock(&memorySpace->Context->SyncObject);
    return allocation;
}

static oserr_t
__ClearPhysicalPages(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size)
{
    paddr_t*   addresses;
    oserr_t osStatus;
    int        pageCount;
    int        pagesCleared = 0;
    int        pagesFreed = 0;
    TRACE("__ClearPhysicalPages(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          memorySpace, address, size);

    // allocate memory for the physical pages
    pageCount = DIVUP(size, GetMemorySpacePageSize());
    addresses = kmalloc(sizeof(paddr_t) * pageCount);
    if (!addresses) {
        osStatus = OsOutOfMemory;
        goto exit;
    }

    // Free the underlying resources first, before freeing the upper resources
    osStatus = ArchMmuClearVirtualPages(memorySpace, address, pageCount,
                                        &addresses[0], &pagesFreed, &pagesCleared);
    if (pagesCleared) {
        // free the physical memory
        if (pagesFreed) {
            FreePhysicalMemory(pagesFreed, &addresses[0]);
        }
        __SyncMemoryRegion(memorySpace, address, size);
    }
    kfree(addresses);

exit:
    TRACE("__ClearPhysicalPages returns=%u", osStatus);
    return osStatus;
}

static oserr_t
__ReleaseAllocation(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size)
{
    struct MemorySpaceAllocation* allocation = NULL;
    size_t                        storedSize = size;
    oserr_t                    osStatus;

    TRACE("__ReleaseAllocation(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          memorySpace, address, size);

    // Support multiple references to an allocation, which means when we try to unmap physical pages
    // we would like to make sure noone actually references them anymore
    if (memorySpace->Context) {
        MutexLock(&memorySpace->Context->SyncObject);
        allocation = __FindAllocation(memorySpace, address);
        if (allocation) {
            allocation->References--;
            if (allocation->References) {
                // still has references so we just free virtual
                MutexUnlock(&memorySpace->Context->SyncObject);
                osStatus = OsOK;
                goto exit;
            }

            storedSize = allocation->Length;
            list_remove(&memorySpace->Context->Allocations, &allocation->Header);
        }
        MutexUnlock(&memorySpace->Context->SyncObject);
    }

    // clear our copy first
    osStatus = __ClearPhysicalPages(memorySpace, address, storedSize);

    // then clear original copy if there was any
    if (allocation) {
        if (allocation->CloneOf) {
            WARNING_IF(allocation->CloneOf->MemorySpace != memorySpace,
                       "__ReleaseAllocation cross memory-space freeing!!! DANGEROUS!!");

            __ReleaseAllocation(allocation->CloneOf->MemorySpace,
                                allocation->CloneOf->Address,
                                allocation->CloneOf->Length);
        }
        kfree(allocation);
    }

exit:
    TRACE("__ReleaseAllocation returns=%u", osStatus);
    return osStatus;
}

static void
__LinkAllocations(
        _In_ MemorySpace_t*                memorySpace,
        _In_ vaddr_t                       address,
        _In_ struct MemorySpaceAllocation* link)
{
    struct MemorySpaceAllocation* allocation;
    TRACE("__LinkAllocations(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ")",
          memorySpace, address);

    MutexLock(&memorySpace->Context->SyncObject);
    allocation = __FindAllocation(memorySpace, address);
    if (allocation) {
        allocation->CloneOf = link;
    }
    MutexUnlock(&memorySpace->Context->SyncObject);
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
    struct MemorySpaceAllocation* sourceAllocation = NULL;
    vaddr_t                       virtualBase;
    int                           pageCount = DIVUP(length, GetMemorySpacePageSize());
    int                           pagesRetrieved;
    int                           pagesUpdated;
    uintptr_t*                    physicalAddressValues = NULL;
    oserr_t                    osStatus;

    TRACE("MemorySpaceCloneMapping(sourceSpace=0x%" PRIxIN ", destinationSpace=0x%" PRIxIN
          ", sourceAddress=0x%" PRIxIN ", length=0x%" PRIxIN ", memoryFlags=0x%x)",
          sourceSpace, destinationSpace, sourceAddress, length, memoryFlags);

    if (!sourceSpace || !destinationSpace || !destinationAddress) {
        return OsInvalidParameters;
    }

    // increase reference of the source allocation first, to "acquire" it.
    sourceAllocation = __AcquireAllocation(sourceSpace, sourceAddress);

    osStatus = __GetAndVerifyPhysicalMapping(
            sourceSpace,
            sourceAddress,
            pageCount,
            &physicalAddressValues,
            &pagesRetrieved
    );
    if (osStatus != OsOK) {
        goto exit;
    }

    virtualBase = __AllocateVirtualMemory(
            destinationSpace,
            destinationAddress,
            length,
            memoryFlags,
            placementFlags
    );
    if (virtualBase == 0) {
        osStatus = OsOutOfMemory;
        goto exit;
    }

    // bind the source and destination allocation together
    if (sourceAllocation) {
        __LinkAllocations(destinationSpace, virtualBase, sourceAllocation);
    }

    osStatus = ArchMmuSetVirtualPages(
            destinationSpace,
            virtualBase,
            &physicalAddressValues[0],
            pagesRetrieved,
            memoryFlags | MAPPING_PERSISTENT | MAPPING_COMMIT,
            &pagesUpdated
    );
    if (osStatus == OsOK && pagesUpdated != pageCount) {
        osStatus = OsIncomplete;
    }

exit:
    if (physicalAddressValues) {
        kfree(physicalAddressValues);
    }

    if (osStatus != OsOK && osStatus != OsIncomplete) {
        if (sourceAllocation) {
            __ReleaseAllocation(sourceSpace, sourceAddress, length);
        }
    }

    TRACE("MemorySpaceCloneMapping returns=%u", osStatus);
    return osStatus;
}

oserr_t
MemorySpaceUnmap(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        address,
        _In_ size_t         size)
{
    oserr_t osStatus;
    TRACE("MemorySpaceUnmap(memorySpace=0x%" PRIxIN ", address=0x%" PRIxIN ", size=0x%" PRIxIN ")",
          memorySpace, address, size);

    if (!memorySpace || !size || !address) {
        return OsInvalidParameters;
    }

    osStatus = __ReleaseAllocation(memorySpace, address, size);
    if (osStatus != OsOK) {
        goto exit;
    }

    // Free the range in either GAM or Process memory
    if (memorySpace->Context != NULL && DynamicMemoryPoolContains(&memorySpace->Context->Heap, address)) {
        DynamicMemoryPoolFree(&memorySpace->Context->Heap, address);
    }
    else if (StaticMemoryPoolContains(&GetMachine()->GlobalAccessMemory, address)) {
        StaticMemoryPoolFree(&GetMachine()->GlobalAccessMemory, address);
    }
    else if (DynamicMemoryPoolContains(&memorySpace->ThreadMemory, address)) {
        DynamicMemoryPoolFree(&memorySpace->ThreadMemory, address);
    }

exit:
    TRACE("MemorySpaceUnmap returns=%u", osStatus);
    return osStatus;
}

oserr_t
MemorySpaceChangeProtection(
        _In_    MemorySpace_t* memorySpace,
        _InOut_ vaddr_t        address,
        _In_    size_t         length,
        _In_    unsigned int   attributes,
        _Out_   unsigned int*  previousAttributes)
{
    int        pageCount = DIVUP((length + (address % GetMemorySpacePageSize())), GetMemorySpacePageSize());
    int        pagesUpdated;
    oserr_t osStatus;

    if (!memorySpace || !length || !previousAttributes) {
        return OsInvalidParameters;
    }

    *previousAttributes = attributes;
    osStatus = ArchMmuUpdatePageAttributes(memorySpace, address, pageCount,
                                           previousAttributes, &pagesUpdated);
    if (osStatus != OsOK && osStatus != OsIncomplete) {
        return osStatus;
    }
    __SyncMemoryRegion(memorySpace, address, length);
    return osStatus;
}

oserr_t
MemorySpaceQuery(
        _In_ MemorySpace_t*      memorySpace,
        _In_ vaddr_t             address,
        _In_ MemoryDescriptor_t* descriptor)
{
    struct MemorySpaceAllocation* allocation;

    if (!memorySpace || !memorySpace->Context) {
        return OsNotExists;
    }

    MutexLock(&memorySpace->Context->SyncObject);
    allocation = __FindAllocation(memorySpace, address);
    MutexUnlock(&memorySpace->Context->SyncObject);

    if (!allocation) {
        return OsNotExists;
    }

    descriptor->StartAddress = allocation->Address;
    descriptor->AllocationSize = allocation->Length;
    descriptor->Attributes = allocation->Flags;
    return OsOK;
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
        return OsInvalidParameters;
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
        return OsInvalidParameters;
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
        return OsInvalidParameters;
    }

    osStatus = ArchMmuGetPageAttributes(memorySpace, address, 1, &flags, &pagesRetrieved);
    if (osStatus == OsOK && !(flags & MAPPING_ISDIRTY)) {
        osStatus = OsError;
    }
    return osStatus;
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
        return OsInvalidParameters;
    }

    osStatus = ArchMmuGetPageAttributes(memorySpace, address, 1, &flags, &pagesRetrieved);
    if (osStatus == OsOK && !(flags & MAPPING_COMMIT)) {
        osStatus = OsNotExists;
    }
    return osStatus;
}

oserr_t
MemorySpaceSetSignalHandler(
        _In_ MemorySpace_t* memorySpace,
        _In_ vaddr_t        signalHandlerAddress)
{
    if (!memorySpace || !memorySpace->Context) {
        return OsInvalidParameters;
    }

    memorySpace->Context->SignalHandler = signalHandlerAddress;
    return OsOK;
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
