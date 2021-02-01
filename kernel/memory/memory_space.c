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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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
#include <ddk/barrier.h>
#include <debug.h>
#include <handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <machine.h>
#include <string.h>
#include <threading.h>

typedef struct MemorySynchronizationObject {
    _Atomic(int) CallsCompleted;
    UUId_t       MemorySpaceHandle;
    uintptr_t    Address;
    size_t       Length;
} MemorySynchronizationObject_t;

static void DestroyMemorySpace(void* resource);

static void
MemorySynchronizationHandler(
    _In_ void* Context)
{
    MemorySynchronizationObject_t * Object            = (MemorySynchronizationObject_t*)Context;
    MemorySpace_t                 *           Current = GetCurrentMemorySpace();
    UUId_t                         CurrentHandle = GetCurrentMemorySpaceHandle();

    // Make sure the current address space is matching
    // If NULL => everyone must update
    // If it matches our parent, we must update
    // If it matches us, we must update
    smp_mb();
    if (Object->MemorySpaceHandle  == UUID_INVALID ||
        Current->ParentHandle      == Object->MemorySpaceHandle || 
        CurrentHandle              == Object->MemorySpaceHandle) {
        CpuInvalidateMemoryCache((void*)Object->Address, Object->Length);
    }
    atomic_fetch_add(&Object->CallsCompleted, 1);
}

static void
SynchronizeMemoryRegion(
        _In_ MemorySpace_t* MemorySpace,
        _In_ uintptr_t            Address,
        _In_ size_t               Length)
{
    // We can easily allocate this object on the stack as the stack is globally
    // visible to all kernel code. This spares us allocation on heap
    MemorySynchronizationObject_t Object = { 
        .Address        = Address,
        .Length         = Length,
        .CallsCompleted = 0
    };
    
    int     NumberOfCores;
    int     NumberOfActiveCores;
    clock_t InterruptedAt;
    size_t  Timeout = 1000;

    // Skip this entire step if there is no multiple cores active
    NumberOfActiveCores = atomic_load(&GetMachine()->NumberOfActiveCores);
    if (NumberOfActiveCores <= 1) {
        return;
    }

    // Check for global address, in that case invalidate all cores
    if (StaticMemoryPoolContains(&GetMachine()->GlobalAccessMemory, Address)) {
        Object.MemorySpaceHandle = UUID_INVALID; // Everyone must update
    }
    else {
        if (MemorySpace->ParentHandle == UUID_INVALID) {
            Object.MemorySpaceHandle = GetCurrentMemorySpaceHandle(); // Children of us must update
        }
        else {
            Object.MemorySpaceHandle = MemorySpace->ParentHandle; // Parent and siblings!
        }
    }
    
    NumberOfCores = ProcessorMessageSend(1, CpuFunctionCustom, MemorySynchronizationHandler, &Object, 1);
    while (atomic_load(&Object.CallsCompleted) != NumberOfCores && Timeout > 0) {
        SchedulerSleep(5, &InterruptedAt);
        Timeout -= 5;
    }
    
    if (!Timeout) {
        ERROR("[memory] [sync] timeout trying to synchronize with cores actual %i != target %i",
            atomic_load(&Object.CallsCompleted), NumberOfCores);
    }
}

static OsStatus_t
CreateMemorySpaceContext(
        _In_ MemorySpace_t* MemorySpace)
{
    MemorySpaceContext_t * Context = (MemorySpaceContext_t*)kmalloc(sizeof(MemorySpaceContext_t));
    if (!Context) {
        return OsOutOfMemory;
    }
    
    DynamicMemoryPoolConstruct(&Context->Heap, GetMachine()->MemoryMap.UserHeap.Start, 
        GetMachine()->MemoryMap.UserHeap.Length, GetMachine()->MemoryGranularity);
    Context->SignalHandler  = 0;
    Context->MemoryHandlers = kmalloc(sizeof(list_t));
    if (!Context->MemoryHandlers) {
        assert(0);
    }
    list_construct(Context->MemoryHandlers);

    MemorySpace->Context = Context;
    return OsSuccess;
}

static void
CleanupMemoryHandler(
    _In_ element_t* Element,
    _In_ void*      Context)
{
    MemoryMappingHandler_t * Handler              = Element->value;
    MemorySpace_t          *          MemorySpace = Context;
    
    DynamicMemoryPoolFree(&MemorySpace->Context->Heap, Handler->Address);
    DestroyHandle(Handler->Handle);
}

static void
DestroyMemorySpaceContext(
        _In_ MemorySpace_t* MemorySpace)
{
    assert(MemorySpace != NULL);
    assert(MemorySpace->Context != NULL);
    
    list_clear(MemorySpace->Context->MemoryHandlers, CleanupMemoryHandler, MemorySpace);
    DynamicMemoryPoolDestroy(&MemorySpace->Context->Heap);
    kfree(MemorySpace->Context->MemoryHandlers);
    kfree(MemorySpace->Context);
}

OsStatus_t
InitializeMemorySpace(
        _In_ MemorySpace_t* SystemMemorySpace)
{
    SystemMemorySpace->ParentHandle = UUID_INVALID;
    SystemMemorySpace->Context      = NULL;
    return InitializeVirtualSpace(SystemMemorySpace);
}

OsStatus_t
CreateMemorySpace(
    _In_  unsigned int Flags,
    _Out_ UUId_t*      Handle)
{
    // If we want to create a new kernel address
    // space we instead want to re-use the current 
    // If kernel is specified, ignore rest 
    if (Flags == MEMORY_SPACE_INHERIT) {
        // Inheritance is a bit different, we re-use again
        // but instead of reusing the kernel, we reuse the current
        *Handle = GetCurrentMemorySpaceHandle();
    }
    else if (Flags & MEMORY_SPACE_APPLICATION) {
        uintptr_t      threadRegionStart = GetMachine()->MemoryMap.ThreadRegion.Start;
        size_t         threadRegionSize  = GetMachine()->MemoryMap.ThreadRegion.Length + 1;
        MemorySpace_t* parent            = NULL;
        MemorySpace_t* memorySpace;

        memorySpace = (MemorySpace_t*)kmalloc(sizeof(MemorySpace_t));
        if (!memorySpace) {
            return OsOutOfMemory;
        }
        memset((void*)memorySpace, 0, sizeof(MemorySpace_t));

        memorySpace->Flags        = Flags;
        memorySpace->ParentHandle = UUID_INVALID;
        DynamicMemoryPoolConstruct(&memorySpace->ThreadMemory, threadRegionStart,
                                   threadRegionSize, GetMemorySpacePageSize());

        // Parent must be the upper-most instance of the address-space
        // of the process. Only to the point of not having kernel as parent
        if (Flags & MEMORY_SPACE_INHERIT) {
            int i;
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
                for (i = 0; i < MEMORY_DATACOUNT; i++) {
                    memorySpace->Data[i] = parent->Data[i];
                }
            }
            else {
                parent = NULL;
            }
        }
        
        // If we are root, create the memory bitmaps
        if (memorySpace->ParentHandle == UUID_INVALID) {
            CreateMemorySpaceContext(memorySpace);
        }
        CloneVirtualSpace(parent, memorySpace, (Flags & MEMORY_SPACE_INHERIT) ? 1 : 0);
        *Handle = CreateHandle(HandleTypeMemorySpace, DestroyMemorySpace, memorySpace);
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in CreateMemorySpace 0x%" PRIxIN "", Flags);
    }
    
    smp_wmb();
    return OsSuccess;
}

void
SwitchMemorySpace(
    _In_ MemorySpace_t* MemorySpace)
{
    ArchMmuSwitchMemorySpace(MemorySpace);
}

MemorySpace_t*
GetCurrentMemorySpace(void)
{
    // Lookup current thread
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

UUId_t
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

OsStatus_t
AreMemorySpacesRelated(
        _In_ MemorySpace_t* Space1,
        _In_ MemorySpace_t* Space2)
{
    return (Space1->Context == Space2->Context) ? OsSuccess : OsError;
}

static VirtualAddress_t
AllocateVirtualMemory(
        _In_ MemorySpace_t* memorySpace,
        _In_ uintptr_t*     virtualAddress,
        _In_ size_t         size,
        _In_ unsigned int   memoryFlags,
        _In_ unsigned int   placementFlags)
{
    VirtualAddress_t virtualBase  = 0;
    unsigned int     virtualFlags = placementFlags & MAPPING_VIRTUAL_MASK;

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
    if (memoryFlags & MAPPING_GUARDPAGE) {
        virtualBase += GetMemorySpacePageSize();
    }

    if (virtualAddress) {
        *virtualAddress = virtualBase;
    }
    return virtualBase;
}

OsStatus_t
MemorySpaceMap(
        _In_    MemorySpace_t* MemorySpace,
        _InOut_ VirtualAddress_t*    Address,
        _InOut_ uintptr_t*           PhysicalAddressValues,
        _In_    size_t               Length,
        _In_    unsigned int         MemoryFlags,
        _In_    unsigned int         PlacementFlags)
{
    int              PageCount = DIVUP(Length, GetMemorySpacePageSize());
    int              PagesUpdated;
    VirtualAddress_t VirtualBase;
    OsStatus_t       Status;
    TRACE("[memory_map] %u, 0x%x, 0x%x", LODWORD(Length), MemoryFlags, PlacementFlags);
    
    // If we are trying to reserve memory through this call, redirect it to the
    // dedicated reservation method. 
    if (!(MemoryFlags & MAPPING_COMMIT)) {
        return MemorySpaceMapReserved(MemorySpace, Address, Length, MemoryFlags, PlacementFlags);
    }
    
    assert(MemorySpace != NULL);
    assert(PhysicalAddressValues != NULL);
    assert(PlacementFlags != 0);
    
    // In case the mappings are provided, we would like to force the COMMIT flag.
    if (PlacementFlags & MAPPING_PHYSICAL_FIXED) {
        MemoryFlags |= MAPPING_COMMIT;
    }
    else if (MemoryFlags & MAPPING_COMMIT) {
        Status = AllocatePhysicalMemory(PageCount, &PhysicalAddressValues[0]);
        if (Status != OsSuccess) {
            return Status;
        }
    }
    
    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    VirtualBase = AllocateVirtualMemory(MemorySpace, Address, Length, MemoryFlags, PlacementFlags);
    if (!VirtualBase) {
        // Cleanup physical mappings
        // TODO
        ERROR("[memory_map] implement cleanup of phys virt");
        return OsInvalidParameters;
    }
    
    Status = ArchMmuSetVirtualPages(MemorySpace, VirtualBase, 
        PhysicalAddressValues, PageCount, MemoryFlags, &PagesUpdated);
    if (Status != OsSuccess) {
        // Handle cleanup of the pages not mapped
        // TODO
        ERROR("[memory_map] implement cleanup of phys/virt");
    }
    return Status;
}

OsStatus_t
MemorySpaceMapContiguous(
        _In_    MemorySpace_t*    MemorySpace,
        _InOut_ VirtualAddress_t* Address,
        _In_    uintptr_t         PhysicalStartAddress,
        _In_    size_t            Length,
        _In_    unsigned int      MemoryFlags,
        _In_    unsigned int      PlacementFlags)
{
    int              PageCount = DIVUP(Length, GetMemorySpacePageSize());
    int              PagesUpdated;
    VirtualAddress_t VirtualBase;
    OsStatus_t       Status;
    
    TRACE("[memory_map_contiguous] %u, 0x%x, 0x%x", 
        LODWORD(Length), MemoryFlags, PlacementFlags);
    
    assert(MemorySpace != NULL);
    assert(PlacementFlags != 0);
    
    // COMMIT must be set when mapping contiguous physical ranges
    MemoryFlags |= MAPPING_COMMIT;
    
    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    VirtualBase = AllocateVirtualMemory(MemorySpace, Address, Length, MemoryFlags, PlacementFlags);
    if (!VirtualBase) {
        return OsInvalidParameters;
    }
    
    Status = ArchMmuSetContiguousVirtualPages(MemorySpace, VirtualBase, 
        PhysicalStartAddress, PageCount, MemoryFlags, &PagesUpdated);
    if (Status != OsSuccess) {
        // Handle cleanup of the pages not mapped
        // TODO
        ERROR("[memory_map_contiguous] implement cleanup");
    }
    return Status;
}

OsStatus_t
MemorySpaceMapReserved(
        _In_    MemorySpace_t*    memorySpace,
        _InOut_ VirtualAddress_t* address,
        _In_    size_t            size,
        _In_    unsigned int      memoryFlags,
        _In_    unsigned int      placementFlags)
{
    int              pageCount = DIVUP(size, GetMemorySpacePageSize());
    int              pagesReserved;
    VirtualAddress_t virtualBase;
    OsStatus_t       osStatus;
    
    TRACE("[memory_map_reserve] %u, 0x%x, 0x%x", LODWORD(size), memoryFlags, placementFlags);

    if (!memorySpace || !address) {
        return OsInvalidParameters;
    }

    // Clear the COMMIT flag if provided
    memoryFlags &= ~(MAPPING_COMMIT);

    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    virtualBase = AllocateVirtualMemory(memorySpace, address, size, memoryFlags, placementFlags);
    if (!virtualBase) {
        return OsInvalidParameters;
    }

    osStatus = ArchMmuReserveVirtualPages(memorySpace, virtualBase, pageCount, memoryFlags, &pagesReserved);
    if (osStatus != OsSuccess) {
        // Handle cleanup of the pages not mapped
        // TODO
        ERROR("[memory_map_reserve] implement cleanup");
    }
    return osStatus;
}

OsStatus_t
MemorySpaceCommit(
        _In_ MemorySpace_t*   memorySpace,
        _In_ VirtualAddress_t address,
        _In_ uintptr_t*       physicalAddressValues,
        _In_ size_t           size,
        _In_ unsigned int     placementFlags)
{
    int        pageCount = DIVUP(size, GetMemorySpacePageSize());
    int        pagesComitted;
    OsStatus_t osStatus;

    if (!memorySpace || !physicalAddressValues) {
        return OsInvalidParameters;
    }

    if (!(placementFlags & MAPPING_PHYSICAL_FIXED)) {
        osStatus = AllocatePhysicalMemory(pageCount, &physicalAddressValues[0]);
        if (osStatus != OsSuccess) {
            return osStatus;
        }
    }

    osStatus = ArchMmuCommitVirtualPage(memorySpace, address, &physicalAddressValues[0],
                                        pageCount, &pagesComitted);
    if (osStatus != OsSuccess) {
        ERROR("[memory] [commit] status %u, comitting address 0x%" PRIxIN ", length 0x%" PRIxIN,
              osStatus, address, size);
        if (!(placementFlags & MAPPING_PHYSICAL_FIXED)) {
            IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
            bounded_stack_push_multiple(&GetMachine()->PhysicalMemory,
                                        (void**)&physicalAddressValues[0], pageCount);
            IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
        }
    }
    return osStatus;
}

OsStatus_t
CloneMemorySpaceMapping(
        _In_        MemorySpace_t*    SourceSpace,
        _In_        MemorySpace_t*    DestinationSpace,
        _In_        VirtualAddress_t  SourceAddress,
        _InOut_Opt_ VirtualAddress_t* DestinationAddress,
        _In_        size_t            Length,
        _In_        unsigned int      MemoryFlags,
        _In_        unsigned int      PlacementFlags)
{
    VirtualAddress_t VirtualBase;
    int              PageCount = DIVUP(Length, GetMemorySpacePageSize());
    int              PagesRetrieved;
    int              PagesUpdated;
    uintptr_t*       PhysicalAddressValues;
    OsStatus_t       Status;
    
    TRACE("[memory] [clone] ");
    
    assert(SourceSpace != NULL);
    assert(DestinationSpace != NULL);
    
    PhysicalAddressValues = (uintptr_t*)kmalloc(PageCount * sizeof(uintptr_t));
    if (!PhysicalAddressValues) {
        return OsOutOfMemory;
    }
    
    // Allocate a temporary array to store physical mappings
    Status = ArchMmuVirtualToPhysical(SourceSpace, SourceAddress, PageCount, 
        &PhysicalAddressValues[0], &PagesRetrieved);
    if (Status != OsSuccess) {
        kfree(PhysicalAddressValues);
        return Status; // Also if the status was OsIncomplete
    }

    // Get the virtual address space, this however may not end up as 0 if it the mapping
    // is not provided already.
    VirtualBase = AllocateVirtualMemory(DestinationSpace,
                                        DestinationAddress, Length, MemoryFlags, PlacementFlags);
    if (VirtualBase == 0) {
        ERROR("[memory] [clone] failed to allocate virtual memory for the cloning of mappings");
        kfree(PhysicalAddressValues);
        return OsError;
    }

    // Add required memory flags
    MemoryFlags |= MAPPING_PERSISTENT | MAPPING_COMMIT;
    
    Status = ArchMmuSetVirtualPages(DestinationSpace, VirtualBase, &PhysicalAddressValues[0], 
        PagesRetrieved, MemoryFlags, &PagesUpdated);
    if (Status == OsSuccess && PagesUpdated != PageCount) {
        Status = OsIncomplete;
    }
    kfree(PhysicalAddressValues);
    return Status;
}

OsStatus_t
MemorySpaceUnmap(
        _In_ MemorySpace_t*   memorySpace,
        _In_ VirtualAddress_t address,
        _In_ size_t           size)
{
    int                pageCount = DIVUP(size, GetMemorySpacePageSize());
    OsStatus_t         osStatus;
    int                pagesCleared = 0;
    int                pagesFreed = 0;
    PhysicalAddress_t* addresses;

    if (!memorySpace || !size || !address) {
        return OsInvalidParameters;
    }

    // allocate memory for the physical pages
    addresses = kmalloc(sizeof(PhysicalAddress_t) * pageCount);
    if (!addresses) {
        return OsOutOfMemory;
    }

    // Free the underlying resources first, before freeing the upper resources
    osStatus = ArchMmuClearVirtualPages(memorySpace, address, pageCount,
                                        &addresses[0], &pagesFreed, &pagesCleared);
    if (osStatus != OsSuccess) {
        WARNING("[memory] [unmap] failed to unmap region 0x%" PRIxIN " of length 0x%" PRIxIN ": %u",
                address, size, osStatus);
    }

    if (pagesCleared) {
        // free the physical memory
        if (pagesFreed) {
            IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
            bounded_stack_push_multiple(&GetMachine()->PhysicalMemory, (void**)&addresses[0], pagesFreed);
            IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
        }
        SynchronizeMemoryRegion(memorySpace, address, size);
    }

    // clean up the address array again
    kfree(addresses);

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
    return OsSuccess;
}

OsStatus_t
MemorySpaceChangeProtection(
        _In_        MemorySpace_t* SystemMemorySpace,
        _InOut_Opt_ VirtualAddress_t     Address,
        _In_        size_t               Length,
        _In_        unsigned int              Attributes,
        _Out_       unsigned int*             PreviousAttributes)
{
    int        PageCount = DIVUP((Length + (Address % GetMemorySpacePageSize())), GetMemorySpacePageSize());
    int        PagesUpdated;
    OsStatus_t Status;

    assert(SystemMemorySpace != NULL);

    *PreviousAttributes = Attributes;
    Status = ArchMmuUpdatePageAttributes(SystemMemorySpace, Address, PageCount,
        PreviousAttributes, &PagesUpdated);
    if (Status != OsSuccess && Status != OsIncomplete) {
        return Status;
    }
    SynchronizeMemoryRegion(SystemMemorySpace, Address, Length);
    return Status;
}

OsStatus_t
GetMemorySpaceMapping(
        _In_  MemorySpace_t* MemorySpace,
        _In_  VirtualAddress_t     Address,
        _In_  int                  PageCount,
        _Out_ uintptr_t*           DmaVectorOut)
{
    OsStatus_t Status;
    int        PagesRetrieved;
    
    assert(MemorySpace != NULL);
    assert(DmaVectorOut != NULL);
    
    Status = ArchMmuVirtualToPhysical(MemorySpace, Address, PageCount, 
        DmaVectorOut, &PagesRetrieved);
    return Status;
}

unsigned int
GetMemorySpaceAttributes(
        _In_ MemorySpace_t* SystemMemorySpace,
        _In_ VirtualAddress_t     VirtualAddress)
{
    unsigned int Attributes;
    int     PagesRetrieved;
    
    assert(SystemMemorySpace != NULL);
    
    if (ArchMmuGetPageAttributes(SystemMemorySpace, VirtualAddress, 1, 
            &Attributes, &PagesRetrieved) != OsSuccess) {
        return 0;
    }
    return Attributes;
}

OsStatus_t
IsMemorySpacePageDirty(
        _In_ MemorySpace_t* SystemMemorySpace,
        _In_ VirtualAddress_t     Address)
{
    OsStatus_t Status = OsSuccess;
    unsigned int    Flags  = 0;
    int        PagesRetrieved;
    
    // Sanitize address space
    assert(SystemMemorySpace != NULL);
    Status = ArchMmuGetPageAttributes(SystemMemorySpace, Address, 1, &Flags, &PagesRetrieved);

    // Check the flags if status was ok
    if (Status == OsSuccess && !(Flags & MAPPING_ISDIRTY)) {
        Status = OsError;
    }
    return Status;
}

OsStatus_t
IsMemorySpacePagePresent(
        _In_ MemorySpace_t* SystemMemorySpace,
        _In_ VirtualAddress_t     Address)
{
    OsStatus_t Status = OsSuccess;
    unsigned int    Flags  = 0;
    int        PagesRetrieved;
    
    // Sanitize address space
    assert(SystemMemorySpace != NULL);
    Status = ArchMmuGetPageAttributes(SystemMemorySpace, Address, 1, &Flags, &PagesRetrieved);

    // Check the flags if status was ok
    if (Status == OsSuccess && !(Flags & MAPPING_COMMIT)) {
        Status = OsDoesNotExist;
    }
    return Status;
}

size_t
GetMemorySpacePageSize(void)
{
    return GetMachine()->MemoryGranularity;
}

static void
DestroyMemorySpace(
        _In_ void* resource)
{
    MemorySpace_t* memorySpace = (MemorySpace_t*)resource;
    if (memorySpace->Flags & MEMORY_SPACE_APPLICATION) {
        DynamicMemoryPoolDestroy(&memorySpace->ThreadMemory);
        DestroyVirtualSpace(memorySpace);
    }
    if (memorySpace->ParentHandle == UUID_INVALID) {
        DestroyMemorySpaceContext(memorySpace);
    }
    if (memorySpace->ParentHandle != UUID_INVALID) {
        DestroyHandle(memorySpace->ParentHandle);
    }
    kfree(memorySpace);
}
