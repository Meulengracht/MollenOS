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

static void
MemorySynchronizationHandler(
    _In_ void* Context)
{
    MemorySynchronizationObject_t* Object        = (MemorySynchronizationObject_t*)Context;
    SystemMemorySpace_t*           Current       = GetCurrentMemorySpace();
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
    _In_ SystemMemorySpace_t* MemorySpace,
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
    _In_ SystemMemorySpace_t* MemorySpace)
{
    SystemMemorySpaceContext_t* Context = (SystemMemorySpaceContext_t*)kmalloc(sizeof(SystemMemorySpaceContext_t));
    if (!Context) {
        return OsOutOfMemory;
    }
    
    DynamicMemoryPoolConstruct(&Context->Heap, GetMachine()->MemoryMap.UserHeap.Start, 
        GetMachine()->MemoryMap.UserHeap.Start + GetMachine()->MemoryMap.UserHeap.Length, 
        GetMachine()->MemoryGranularity);
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
    SystemMemoryMappingHandler_t* Handler = Element->value;
    SystemMemorySpace_t*          MemorySpace = Context;
    
    DynamicMemoryPoolFree(&MemorySpace->Context->Heap, Handler->Address);
    DestroyHandle(Handler->Handle);
}

static void
DestroyMemorySpaceContext(
    _In_ SystemMemorySpace_t* MemorySpace)
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
    _In_ SystemMemorySpace_t* SystemMemorySpace)
{
    SystemMemorySpace->ParentHandle = UUID_INVALID;
    SystemMemorySpace->Context      = NULL;
    return InitializeVirtualSpace(SystemMemorySpace);
}

OsStatus_t
CreateMemorySpace(
    _In_  Flags_t Flags,
    _Out_ UUId_t* Handle)
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
        SystemMemorySpace_t* Parent      = NULL;
        SystemMemorySpace_t* MemorySpace = (SystemMemorySpace_t*)kmalloc(sizeof(SystemMemorySpace_t));
        memset((void*)MemorySpace, 0, sizeof(SystemMemorySpace_t));

        MemorySpace->Flags        = Flags;
        MemorySpace->ParentHandle = UUID_INVALID;

        // Parent must be the upper-most instance of the address-space
        // of the process. Only to the point of not having kernel as parent
        if (Flags & MEMORY_SPACE_INHERIT) {
            int i;
            Parent = GetCurrentMemorySpace();
            if (Parent != GetDomainMemorySpace()) {
                if (Parent->ParentHandle != UUID_INVALID) {
                    MemorySpace->ParentHandle = Parent->ParentHandle;
                    MemorySpace->Context      = Parent->Context;
                    Parent                    = (SystemMemorySpace_t*)LookupHandleOfType(
                        Parent->ParentHandle, HandleTypeMemorySpace);
                }
                else {
                    MemorySpace->ParentHandle = GetCurrentMemorySpaceHandle();
                    MemorySpace->Context      = Parent->Context;
                }

                // Add a reference and copy data
                AcquireHandle(MemorySpace->ParentHandle);
                for (i = 0; i < MEMORY_DATACOUNT; i++) {
                    MemorySpace->Data[i] = Parent->Data[i];
                }
            }
            else {
                Parent = NULL;
            }
        }
        
        // If we are root, create the memory bitmaps
        if (MemorySpace->ParentHandle == UUID_INVALID) {
            CreateMemorySpaceContext(MemorySpace);
        }
        CloneVirtualSpace(Parent, MemorySpace, (Flags & MEMORY_SPACE_INHERIT) ? 1 : 0);
        *Handle = CreateHandle(HandleTypeMemorySpace, DestroyMemorySpace, MemorySpace);
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in CreateMemorySpace 0x%" PRIxIN "", Flags);
    }
    
    smp_wmb();
    return OsSuccess;
}

void
DestroyMemorySpace(
    _In_ void* Resource)
{
    SystemMemorySpace_t* MemorySpace = (SystemMemorySpace_t*)Resource;
    if (MemorySpace->Flags & MEMORY_SPACE_APPLICATION) {
        DestroyVirtualSpace(MemorySpace);
    }
    if (MemorySpace->ParentHandle == UUID_INVALID) {
        DestroyMemorySpaceContext(MemorySpace);
    }
    if (MemorySpace->ParentHandle != UUID_INVALID) {
        DestroyHandle(MemorySpace->ParentHandle);
    }
    kfree(MemorySpace);
}

void
SwitchMemorySpace(
    _In_ SystemMemorySpace_t* MemorySpace)
{
    ArchMmuSwitchMemorySpace(MemorySpace);
}

SystemMemorySpace_t*
GetCurrentMemorySpace(void)
{
    // Lookup current thread
    MCoreThread_t *CurrentThread = GetCurrentThreadForCore(ArchGetProcessorCoreId());

    // if no threads are active return the kernel address space
    if (CurrentThread == NULL) {
        return GetDomainMemorySpace();
    }
    else {
        assert(CurrentThread->MemorySpace != NULL);
        return CurrentThread->MemorySpace;
    }
}

UUId_t
GetCurrentMemorySpaceHandle(void)
{
    MCoreThread_t* CurrentThread = GetCurrentThreadForCore(ArchGetProcessorCoreId());
    if (CurrentThread == NULL) {
        return UUID_INVALID;
    }
    else {
        return CurrentThread->MemorySpaceHandle;
    }
}

SystemMemorySpace_t*
GetDomainMemorySpace(void)
{
    return (GetCurrentDomain() != NULL) ? &GetCurrentDomain()->SystemSpace : &GetMachine()->SystemSpace;
}

OsStatus_t
AreMemorySpacesRelated(
    _In_ SystemMemorySpace_t* Space1,
    _In_ SystemMemorySpace_t* Space2)
{
    return (Space1->Context == Space2->Context) ? OsSuccess : OsError;
}

OsStatus_t
MemoryCreateSharedRegion(
    _In_  size_t  Length,
    _In_  size_t  Capacity,
    _In_  Flags_t Flags,
    _Out_ void**  Memory,
    _Out_ UUId_t* Handle)
{
    SystemSharedRegion_t* Region;
    OsStatus_t            Status;
    int                   PageCount;

    // Capacity is the expected maximum size of the region. Regions
    // are resizable, but to ensure that enough continious space is
    // allocated we must do it like this. Otherwise one must create a new.
    PageCount = DIVUP(Capacity, GetMemorySpacePageSize());
    Region    = (SystemSharedRegion_t*)kmalloc(
        sizeof(SystemSharedRegion_t) + (sizeof(uintptr_t) * PageCount));
    if (!Region) {
        return OsOutOfMemory;
    }
    memset(Region, 0, sizeof(SystemSharedRegion_t) + (sizeof(uintptr_t) * PageCount));
    
    // This is more tricky, for the calling process we must make a new
    // mapping that spans the entire Capacity, but is uncommitted, and then commit
    // the Length of it.
    Status = MemorySpaceMapReserved(GetCurrentMemorySpace(),
        (VirtualAddress_t*)Memory, Capacity, 
        MAPPING_USERSPACE | MAPPING_PERSISTENT | Flags,
        MAPPING_VIRTUAL_PROCESS);
    if (Status != OsSuccess) {
        kfree(Region);
        return Status;
    }
    
    // Now commit <Length> in pages
    Status = MemorySpaceCommit(GetCurrentMemorySpace(),
        (VirtualAddress_t)*Memory, &Region->Pages[0], Length, 0);
    
    MutexConstruct(&Region->SyncObject, MUTEX_PLAIN);
    Region->Flags     = Flags;
    Region->Length    = Length;
    Region->Capacity  = Capacity;
    Region->PageCount = PageCount;
    
    *Handle = CreateHandle(HandleTypeMemoryRegion, MemoryDestroySharedRegion, Region);
    return Status;
}

OsStatus_t
MemoryExportSharedRegion(
    _In_  void*   Memory,
    _In_  size_t  Length,
    _In_  Flags_t Flags,
    _Out_ UUId_t* HandleOut)
{
    SystemSharedRegion_t* Region;
    OsStatus_t            Status;
    int                   PageCount;
    size_t                CapacityWithOffset;

    // Capacity is the expected maximum size of the region. Regions
    // are resizable, but to ensure that enough continious space is
    // allocated we must do it like this. Otherwise one must create a new.
    CapacityWithOffset = Length + ((uintptr_t)Memory % GetMemorySpacePageSize());
    PageCount          = DIVUP(CapacityWithOffset, GetMemorySpacePageSize());
    
    Region = (SystemSharedRegion_t*)kmalloc(
        sizeof(SystemSharedRegion_t) + (sizeof(uintptr_t) * PageCount));
    if (!Region) {
        return OsOutOfMemory;
    }
    memset(Region, 0, sizeof(SystemSharedRegion_t) + (sizeof(uintptr_t) * PageCount));
    
    Status = GetMemorySpaceMapping(GetCurrentMemorySpace(), 
        (uintptr_t)Memory, PageCount, &Region->Pages[0]);
    if (Status != OsSuccess) {
        kfree(Region);
        return Status;
    }
    
    MutexConstruct(&Region->SyncObject, MUTEX_PLAIN);
    Region->Flags     = Flags;
    Region->Length    = CapacityWithOffset;
    Region->Capacity  = CapacityWithOffset;
    Region->PageCount = PageCount;
    
    *HandleOut = CreateHandle(HandleTypeMemoryRegion, MemoryDestroySharedRegion, Region);
    return Status;
}

OsStatus_t
MemoryResizeSharedRegion(
    _In_ UUId_t Handle,
    _In_ void*  Memory,
    _In_ size_t NewLength)
{
    SystemSharedRegion_t* Region;
    int                   CurrentPages;
    int                   NewPages;
    uintptr_t             End;
    OsStatus_t            Status;
    
    // Lookup region
    Region = LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    // Verify that the new length is not exceeding capacity
    if (NewLength > Region->Capacity) {
        return OsInvalidParameters;
    }
    
    MutexLock(&Region->SyncObject);
    CurrentPages = DIVUP(Region->Length, GetMemorySpacePageSize());
    NewPages     = DIVUP(NewLength, GetMemorySpacePageSize());
    
    // If we are shrinking (not supported atm) or equal then simply move on
    // and report success. We won't perform any unmapping
    if (CurrentPages >= NewPages) {
        MutexUnlock(&Region->SyncObject);
        return OsSuccess;
    }
    
    // Calculate from where we should start committing new pages
    End    = (uintptr_t)Memory + (CurrentPages * GetMemorySpacePageSize());
    Status = MemorySpaceCommit(GetCurrentMemorySpace(), End, 
        &Region->Pages[CurrentPages], NewLength - Region->Length, 0);
    if (Status == OsSuccess) {
        Region->Length = NewLength;
    }
    MutexUnlock(&Region->SyncObject);
    return Status;
}

OsStatus_t
MemoryRefreshSharedRegion(
    _In_  UUId_t  Handle,
    _In_  void*   Memory,
    _In_  size_t  CurrentLength,
    _Out_ size_t* NewLength)
{
    SystemSharedRegion_t* Region;
    int                   CurrentPages;
    int                   NewPages;
    uintptr_t             End;
    OsStatus_t            Status;
    
    // Lookup region
    Region = LookupHandleOfType(Handle, HandleTypeMemoryRegion);
    if (!Region) {
        return OsDoesNotExist;
    }
    
    MutexLock(&Region->SyncObject);
    
    // Update the out first
    *NewLength = Region->Length;
    
    // Calculate the new number of pages that should be mapped,
    // but instead of using the provided argument as new, it must be
    // the previous
    CurrentPages = DIVUP(CurrentLength, GetMemorySpacePageSize());
    NewPages     = DIVUP(Region->Length, GetMemorySpacePageSize());
    
    // If we are shrinking (not supported atm) or equal then simply move on
    // and report success. We won't perform any unmapping
    if (CurrentPages >= NewPages) {
        MutexUnlock(&Region->SyncObject);
        return OsSuccess;
    }
    
    // Otherwise commit mappings, but instead of doing like the Resize
    // operation we will tell that we provide them ourself
    End = (uintptr_t)Memory + (CurrentPages * GetMemorySpacePageSize());
    Status = MemorySpaceCommit(GetCurrentMemorySpace(), End, 
        &Region->Pages[CurrentPages], Region->Length - CurrentLength,
        MAPPING_PHYSICAL_FIXED);
    MutexUnlock(&Region->SyncObject);
    return Status;
}

void
MemoryDestroySharedRegion(
    _In_ void* Resource)
{
    SystemSharedRegion_t* Region = (SystemSharedRegion_t*)Resource;
    if (!(Region->Flags & MAPPING_PERSISTENT)) {
        IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
        bounded_stack_push_multiple(&GetMachine()->PhysicalMemory,
            (void**)&Region->Pages[0], Region->PageCount);
        IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
    }
    kfree(Region);
}

static VirtualAddress_t
ResolveVirtualSystemMemorySpaceAddress(
    _In_ SystemMemorySpace_t* SystemMemorySpace,
    _In_ uintptr_t*           VirtualAddress,
    _In_ size_t               Size,
    _In_ Flags_t              PlacementFlags)
{
    VirtualAddress_t VirtualBase  = 0;
    Flags_t          VirtualFlags = PlacementFlags & MAPPING_VIRTUAL_MASK;

    switch (VirtualFlags) {
        case MAPPING_VIRTUAL_FIXED: {
            assert(VirtualAddress != NULL);
            VirtualBase = *VirtualAddress;
        } break;

        case MAPPING_VIRTUAL_PROCESS: {
            assert(SystemMemorySpace->Context != NULL);
            VirtualBase = DynamicMemoryPoolAllocate(&SystemMemorySpace->Context->Heap, Size);
            if (VirtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%" PRIxIN " (heap)", Size);
            }
        } break;

        case MAPPING_VIRTUAL_GLOBAL: {
            VirtualBase = StaticMemoryPoolAllocate(&GetMachine()->GlobalAccessMemory, Size);
            if (VirtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%" PRIxIN " (ga-memory)", Size);
            }
        } break;

        default: {
            FATAL(FATAL_SCOPE_KERNEL, "Failed to allocate virtual memory for flags: 0x%" PRIxIN "", VirtualFlags);
        } break;
    }
    assert(VirtualBase != 0);

    if (VirtualAddress != NULL) {
        *VirtualAddress = VirtualBase;
    }
    return VirtualBase;
}

OsStatus_t
MemorySpaceMap(
    _In_    SystemMemorySpace_t* MemorySpace,
    _InOut_ VirtualAddress_t*    Address,
    _InOut_ uintptr_t*           PhysicalAddressValues,
    _In_    size_t               Length,
    _In_    Flags_t              MemoryFlags,
    _In_    Flags_t              PlacementFlags)
{
    int              PageCount = DIVUP(Length, GetMemorySpacePageSize());
    int              PagesUpdated;
    VirtualAddress_t VirtualBase;
    OsStatus_t       Status;
    TRACE("[memory_map] %u, 0x%x, 0x%x", 
        LODWORD(Length), MemoryFlags, PlacementFlags);
    
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
        IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
        bounded_stack_pop_multiple(&GetMachine()->PhysicalMemory,
            (void**)&PhysicalAddressValues[0], PageCount);
        IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
    }
    
    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    VirtualBase = ResolveVirtualSystemMemorySpaceAddress(MemorySpace,
        Address, Length, PlacementFlags);
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
    _In_    SystemMemorySpace_t* MemorySpace,
    _InOut_ VirtualAddress_t*    Address,
    _In_    uintptr_t            PhysicalStartAddress,
    _In_    size_t               Length,
    _In_    Flags_t              MemoryFlags,
    _In_    Flags_t              PlacementFlags)
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
    VirtualBase = ResolveVirtualSystemMemorySpaceAddress(MemorySpace,
        Address, Length, PlacementFlags);
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
    _In_    SystemMemorySpace_t* MemorySpace,
    _InOut_ VirtualAddress_t*    Address,
    _In_    size_t               Length,
    _In_    Flags_t              MemoryFlags,
    _In_    Flags_t              PlacementFlags)
{
    int              PageCount = DIVUP(Length, GetMemorySpacePageSize());
    int              PagesReserved;
    VirtualAddress_t VirtualBase;
    OsStatus_t       Status;
    
    TRACE("[memory_map_reserve] %u, 0x%x, 0x%x", 
        LODWORD(Length), MemoryFlags, PlacementFlags);
    
    assert(MemorySpace != NULL);
    assert(PlacementFlags != 0);
    
    // Clear the COMMIT flag if provided
    MemoryFlags &= ~(MAPPING_COMMIT);
    
    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    VirtualBase = ResolveVirtualSystemMemorySpaceAddress(MemorySpace,
        Address, Length, PlacementFlags);
    if (!VirtualBase) {
        return OsInvalidParameters;
    }
    
    Status = ArchMmuReserveVirtualPages(MemorySpace, VirtualBase, PageCount, 
        MemoryFlags, &PagesReserved);
    if (Status != OsSuccess) {
        // Handle cleanup of the pages not mapped
        // TODO
        ERROR("[memory_map_reserve] implement cleanup");
    }
    return Status;
}

OsStatus_t
MemorySpaceCommit(
    _In_ SystemMemorySpace_t* MemorySpace,
    _In_ VirtualAddress_t     Address,
    _In_ uintptr_t*           PhysicalAddressValues,
    _In_ size_t               Length,
    _In_ Flags_t              Placement)
{
    int        PageCount = DIVUP(Length, GetMemorySpacePageSize());
    int        PagesComitted;
    OsStatus_t Status;
    
    assert(MemorySpace != NULL);
    assert(PhysicalAddressValues != NULL);

    if (!(Placement & MAPPING_PHYSICAL_FIXED)) {
        IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
        bounded_stack_pop_multiple(&GetMachine()->PhysicalMemory, 
            (void**)&PhysicalAddressValues[0], PageCount);
        IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
    }

    Status = ArchMmuCommitVirtualPage(MemorySpace, Address, &PhysicalAddressValues[0],
        PageCount, &PagesComitted);
    if (Status != OsSuccess) {
        ERROR("[memory] [commit] status %u, comitting address 0x%" PRIxIN ", length 0x%" PRIxIN,
            Status, Address, Length);
        NOTIMPLEMENTED("[memory] [commit] implement cleanup of allocated pages");
    }
    return Status;
}

OsStatus_t
CloneMemorySpaceMapping(
    _In_        SystemMemorySpace_t* SourceSpace,
    _In_        SystemMemorySpace_t* DestinationSpace,
    _In_        VirtualAddress_t     SourceAddress,
    _InOut_Opt_ VirtualAddress_t*    DestinationAddress,
    _In_        size_t               Length,
    _In_        Flags_t              MemoryFlags,
    _In_        Flags_t              PlacementFlags)
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
    VirtualBase = ResolveVirtualSystemMemorySpaceAddress(DestinationSpace,
        DestinationAddress, Length, PlacementFlags);
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
    _In_ SystemMemorySpace_t* MemorySpace, 
    _In_ VirtualAddress_t     Address, 
    _In_ size_t               Size)
{
    OsStatus_t Status;
    int        PageCount    = DIVUP(Size, GetMemorySpacePageSize());
    int        PagesCleared = 0;
    assert(MemorySpace != NULL);

    // Free the underlying resources first, before freeing the upper resources
    Status = ArchMmuClearVirtualPages(MemorySpace, Address, PageCount, &PagesCleared);
    if (PagesCleared) {
        SynchronizeMemoryRegion(MemorySpace, Address, Size);
    }
    
    if (Status != OsSuccess) {
        WARNING("[memory] [unmap] failed to unmap region 0x%" PRIxIN " of length 0x%" PRIxIN ": %u",
            Address, Size, Status);
    }

    // Free the range in either GAM or Process memory
    if (MemorySpace->Context != NULL && DynamicMemoryPoolContains(&MemorySpace->Context->Heap, Address)) {
        DynamicMemoryPoolFree(&MemorySpace->Context->Heap, Address);
    }
    else if (StaticMemoryPoolContains(&GetMachine()->GlobalAccessMemory, Address)) {
        StaticMemoryPoolFree(&GetMachine()->GlobalAccessMemory, Address);
    }
    else {
        // Ignore
    }
    return OsSuccess;
}

OsStatus_t
MemorySpaceChangeProtection(
    _In_        SystemMemorySpace_t* SystemMemorySpace,
    _InOut_Opt_ VirtualAddress_t     Address, 
    _In_        size_t               Length, 
    _In_        Flags_t              Attributes,
    _Out_       Flags_t*             PreviousAttributes)
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
    _In_  SystemMemorySpace_t* MemorySpace, 
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

Flags_t
GetMemorySpaceAttributes(
    _In_ SystemMemorySpace_t* SystemMemorySpace, 
    _In_ VirtualAddress_t     VirtualAddress)
{
    Flags_t Attributes;
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
    _In_ SystemMemorySpace_t* SystemMemorySpace,
    _In_ VirtualAddress_t     Address)
{
    OsStatus_t Status = OsSuccess;
    Flags_t    Flags  = 0;
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
    _In_ SystemMemorySpace_t* SystemMemorySpace,
    _In_ VirtualAddress_t     Address)
{
    OsStatus_t Status = OsSuccess;
    Flags_t    Flags  = 0;
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
