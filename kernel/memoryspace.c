/* MollenOS
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

#include <component/cpu.h>
#include <arch/mmu.h>
#include <arch/utils.h>
#include <memoryspace.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <assert.h>
#include <string.h>
#include <debug.h>
#include <heap.h>

typedef struct {
    volatile int CallsCompleted;
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
    if (Object->MemorySpaceHandle  == UUID_INVALID ||
        Current->ParentHandle      == Object->MemorySpaceHandle || 
        CurrentHandle              == Object->MemorySpaceHandle) {
        CpuInvalidateMemoryCache((void*)Object->Address, Object->Length);
    }
    Object->CallsCompleted++;
}

static void
SynchronizeMemoryRegion(
    _In_ SystemMemorySpace_t* SystemMemorySpace,
    _In_ uintptr_t            Address,
    _In_ size_t               Length)
{
    // We can easily allocate this object on the stack as the stack is globally
    // visible to all kernel code. This spares us allocation on heap
    MemorySynchronizationObject_t Object = { 0 };
    int                           NumberOfCores;

    // Skip this entire step if there is no multiple cores active
    if (GetMachine()->NumberOfActiveCores <= 1) {
        return;
    }

    // Check for global address, in that case invalidate all cores
    if (BlockBitmapValidateState(&GetMachine()->GlobalAccessMemory, Address, 1) != OsDoesNotExist) {
        Object.MemorySpaceHandle = UUID_INVALID; // Everyone must update
    }
    else {
        if (SystemMemorySpace->ParentHandle == UUID_INVALID) {
            Object.MemorySpaceHandle = GetCurrentMemorySpaceHandle(); // Children of us must update
        }
        else {
            Object.MemorySpaceHandle = SystemMemorySpace->ParentHandle; // Parent and siblings!
        }
    }
    Object.Address        = Address;
    Object.Length         = Length;
    Object.CallsCompleted = 0;

    NumberOfCores = ExecuteProcessorFunction(1, CpuFunctionCustom,
        MemorySynchronizationHandler, (void*)&Object);
    while (Object.CallsCompleted != NumberOfCores);
}

static void
CreateMemorySpaceContext(
    _In_ SystemMemorySpace_t* MemorySpace)
{
    SystemMemorySpaceContext_t* Context = (SystemMemorySpaceContext_t*)kmalloc(sizeof(SystemMemorySpaceContext_t));
    CreateBlockmap(0, GetMachine()->MemoryMap.UserHeap.Start, 
        GetMachine()->MemoryMap.UserHeap.Start + GetMachine()->MemoryMap.UserHeap.Length, 
        GetMachine()->MemoryGranularity, &Context->HeapSpace);
    Context->MemoryHandlers = CollectionCreate(KeyId);
    Context->SignalHandler  = 0;

    MemorySpace->Context = Context;
}

static void
DestroyMemorySpaceContext(
    _In_ SystemMemorySpace_t* MemorySpace)
{
    assert(MemorySpace != NULL);
    assert(MemorySpace->Context != NULL);
    
    // Destroy all memory handlers
    foreach(Node, MemorySpace->Context->MemoryHandlers) {
        SystemMemoryMappingHandler_t* Handler = (SystemMemoryMappingHandler_t*)Node;
        ReleaseBlockmapRegion(MemorySpace->Context->HeapSpace, Handler->Address, Handler->Length);
        DestroyHandle(Handler->Handle);
    }
    CollectionDestroy(MemorySpace->Context->MemoryHandlers);
    DestroyBlockmap(MemorySpace->Context->HeapSpace);
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
                    Parent                    = (SystemMemorySpace_t*)LookupHandle(Parent->ParentHandle);
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
        *Handle = CreateHandle(HandleTypeMemorySpace, 0, DestroyMemorySpace, MemorySpace);
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in CreateMemorySpace 0x%" PRIxIN "", Flags);
    }
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

OsStatus_t
SwitchMemorySpace(
    _In_ SystemMemorySpace_t* SystemMemorySpace)
{
    return SwitchVirtualSpace(SystemMemorySpace);
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
    Status = CreateMemorySpaceMapping(GetCurrentMemorySpace(),
        (VirtualAddress_t*)Memory, NULL, Capacity, 
        MAPPING_USERSPACE | MAPPING_PERSISTENT | Flags,
        MAPPING_PHYSICAL_DEFAULT | MAPPING_VIRTUAL_PROCESS, __MASK);
    if (Status != OsSuccess) {
        kfree(Region);
        return Status;
    }
    
    // Now commit <Length> in pages
    Status = CommitMemorySpaceMapping(GetCurrentMemorySpace(),
        (VirtualAddress_t)*Memory, &Region->Pages[0], Length, 
        MAPPING_PHYSICAL_DEFAULT, __MASK);
    
    MutexConstruct(&Region->SyncObject, MUTEX_PLAIN);
    Region->Flags     = Flags;
    Region->Length    = Length;
    Region->Capacity  = Capacity;
    Region->PageCount = PageCount;
    
    *Handle = CreateHandle(HandleTypeMemoryRegion, 0, MemoryDestroySharedRegion, Region);
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
    Region->Length    = Length;
    Region->Capacity  = Length;
    Region->PageCount = PageCount;
    
    *HandleOut = CreateHandle(HandleTypeMemoryRegion, 0, MemoryDestroySharedRegion, Region);
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
    Status = CommitMemorySpaceMapping(GetCurrentMemorySpace(), End, 
        &Region->Pages[CurrentPages], NewLength - Region->Length,
        MAPPING_PHYSICAL_DEFAULT, __MASK);
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
    Status = CommitMemorySpaceMapping(GetCurrentMemorySpace(), End, 
        &Region->Pages[CurrentPages], Region->Length - CurrentLength,
        MAPPING_PHYSICAL_FIXED, __MASK);
    MutexUnlock(&Region->SyncObject);
    return Status;
}

void
MemoryDestroySharedRegion(
    _In_ void* Resource)
{
    SystemSharedRegion_t* Region = (SystemSharedRegion_t*)Resource;
    if (!(Region->Flags & MAPPING_PERSISTENT)) {
        for (int i = 0; i < Region->PageCount; i++) {
            FreeSystemMemory(Region->Pages[i], GetMemorySpacePageSize());
        }
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
            VirtualBase = AllocateBlocksInBlockmap(SystemMemorySpace->Context->HeapSpace, __MASK, Size);
            if (VirtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%" PRIxIN " (heap)", Size);
            }
        } break;

        case MAPPING_VIRTUAL_GLOBAL: {
            VirtualBase = AllocateBlocksInBlockmap(&GetMachine()->GlobalAccessMemory, __MASK, Size);
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

static inline OsStatus_t
InstallMemoryMapping(
    _In_ SystemMemorySpace_t* SystemMemorySpace,
    _In_ PhysicalAddress_t    PhysicalAddress,
    _In_ VirtualAddress_t     VirtualAddress,
    _In_ Flags_t              MemoryFlags,
    _In_ Flags_t              PlacementFlags)
{
    OsStatus_t Status = SetVirtualPageMapping(SystemMemorySpace, PhysicalAddress, VirtualAddress, MemoryFlags);
    if (Status != OsSuccess) {
        if (Status == OsExists) {
            ERROR("Memory mapping at 0x%" PRIxIN " already existed", VirtualAddress);
            assert((PlacementFlags & MAPPING_VIRTUAL_FIXED) != 0);
        }
    }
    return Status;
}

OsStatus_t
CreateMemorySpaceMapping(
    _In_        SystemMemorySpace_t* MemorySpace,
    _InOut_     VirtualAddress_t*    Address,
    _InOut_Opt_ uintptr_t*           DmaVector,
    _In_        size_t               Length,
    _In_        Flags_t              MemoryFlags,
    _In_        Flags_t              PlacementFlags,
    _In_        uintptr_t            PhysicalMask)
{
    int               PageCount      = DIVUP(Length, GetMemorySpacePageSize());
    int               CleanupOnError = 0;
    VirtualAddress_t  VirtualBase;
    OsStatus_t        Status;
    int               i;
    
    TRACE("CreateMemorySpaceMapping(%u, 0x%x, 0x%x)", LODWORD(Length), MemoryFlags, PlacementFlags);
    
    assert(MemorySpace != NULL);
    assert(PlacementFlags != 0);
    
    // Make sure that we set COMMIT and PERSISTANT if fixed is present, it makes no sense to not
    // commit fixed addresses as persistent
    if (PlacementFlags & MAPPING_PHYSICAL_FIXED) {
        MemoryFlags |= MAPPING_COMMIT | MAPPING_PERSISTENT;
    }
    
    // Handle the resolvement of the physical address, only do this if we are
    // not to save the allocations and we haven't been supplied
    if (DmaVector != NULL && (MemoryFlags & MAPPING_COMMIT) && 
            (PlacementFlags & MAPPING_PHYSICAL_DEFAULT)) {
        CleanupOnError = 1;
        for (i = 0; i < PageCount; i++) {
            DmaVector[i] = AllocateSystemMemory(GetMemorySpacePageSize(), PhysicalMask, 0);
            if (!DmaVector[i]) {
                for (i -= 1; i > 0; i--) {
                    FreeSystemMemory(DmaVector[i], GetMemorySpacePageSize());
                }
                return OsOutOfMemory;
            }
        }
    }
    
    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    VirtualBase = ResolveVirtualSystemMemorySpaceAddress(MemorySpace,
        Address, Length, PlacementFlags);
    if (VirtualBase != 0) {
        for (i = 0; i < PageCount; i++) {
            uintptr_t VirtualPage  = VirtualBase + (i * GetMemorySpacePageSize());
            uintptr_t PhysicalPage = 0;
            
            // Physical page can come from three places
            // None 
            // DmaVector
            // On-the-fly 
            if (MemoryFlags & MAPPING_COMMIT) {
                if (PlacementFlags & (MAPPING_PHYSICAL_FIXED | MAPPING_PHYSICAL_DEFAULT)) {
                    if ((PlacementFlags & MAPPING_PHYSICAL_CONTIGIOUS) == MAPPING_PHYSICAL_CONTIGIOUS) {
                        PhysicalPage = DmaVector[0] + (i * GetMemorySpacePageSize());
                    }
                    else if (DmaVector) {
                        PhysicalPage = DmaVector[i];
                    }
                    else {
                        PhysicalPage = AllocateSystemMemory(GetMemorySpacePageSize(), PhysicalMask, 0);
                    }
                }
            }
            
            Status = InstallMemoryMapping(MemorySpace, PhysicalPage, 
                VirtualPage, MemoryFlags, PlacementFlags);
            if (Status != OsSuccess) {
                if ((MemoryFlags & MAPPING_COMMIT) &&
                    (PlacementFlags & MAPPING_PHYSICAL_DEFAULT) && 
                    !DmaVector) {
                    FreeSystemMemory(PhysicalPage, GetMemorySpacePageSize());
                }
                break;
            }
        }

        // If we don't reach end of loop, should we undo?
        if (i != PageCount) {
            for (i -= 1; i >= 0; i--) {
                uintptr_t VirtualPage  = (VirtualBase + (i * GetMemorySpacePageSize()));
                ClearVirtualPageMapping(MemorySpace, VirtualPage);
            }
        }
    }

    // Cleanup if we had preallocated space and we failed to map it in
    if (CleanupOnError && Status != OsSuccess) {
        for (i = 0; i < PageCount; i++) {
            FreeSystemMemory(DmaVector[i], GetMemorySpacePageSize());
        }
    }
    return Status;
}

OsStatus_t
CommitMemorySpaceMapping(
    _In_        SystemMemorySpace_t* MemorySpace,
    _In_        VirtualAddress_t     Address,
    _In_        uintptr_t*           DmaVector,
    _In_        size_t               Length,
    _In_        Flags_t              Placement,
    _In_        uintptr_t            Mask)
{
    int        PageCount = DIVUP(Length, GetMemorySpacePageSize());
    OsStatus_t Status;
    int        i;
    assert(MemorySpace != NULL);

    // Make sure DmaVector is provided in this case
    if (Placement & MAPPING_PHYSICAL_FIXED) {
        assert(DmaVector != NULL);
    }

    for (i = 0; i < PageCount; i++) {
        uintptr_t Virtual = Address + (i * GetMemorySpacePageSize());
        uintptr_t Dma;
        
        if (Placement & MAPPING_PHYSICAL_FIXED) {
            if ((Placement & MAPPING_PHYSICAL_CONTIGIOUS) == MAPPING_PHYSICAL_CONTIGIOUS) {
                Dma = DmaVector[0] + (i * GetMemorySpacePageSize());
            }
            else {
                Dma = DmaVector[i];
            }
        }
        else {
            Dma = AllocateSystemMemory(GetMemorySpacePageSize(), Mask, 0);
            if (DmaVector != NULL) {
                DmaVector[i] = Dma;
            }
        }
        
        Status = CommitVirtualPageMapping(MemorySpace, Dma, Virtual);
        if (Status != OsSuccess) {
            if (Placement & MAPPING_PHYSICAL_DEFAULT) {
                FreeSystemMemory(Dma, GetMemorySpacePageSize());
            }
            break;
        }
    }
    return Status;
}

OsStatus_t
CloneMemorySpaceMapping(
    _In_        SystemMemorySpace_t* SourceSpace,
    _In_        SystemMemorySpace_t* DestinationSpace,
    _In_        VirtualAddress_t     SourceAddress,
    _InOut_Opt_ VirtualAddress_t*    DestinationAddress,
    _In_        size_t               Size,
    _In_        Flags_t              MemoryFlags,
    _In_        Flags_t              PlacementFlags)
{
    VirtualAddress_t VirtualBase;
    int              PageCount = DIVUP(Size, GetMemorySpacePageSize());
    uintptr_t        DmaVector[PageCount];
    int              i;
    OsStatus_t       Status;
    assert(SourceSpace != NULL);
    assert(DestinationSpace != NULL);
    
    // Allocate a temporary array to store physical mappings
    Status = GetMemorySpaceMapping(SourceSpace, SourceAddress, PageCount, &DmaVector[0]);

    // Get the virtual address space, this however may not end up as 0 if it the mapping
    // is not provided already.
    VirtualBase = ResolveVirtualSystemMemorySpaceAddress(DestinationSpace, DestinationAddress, Size, PlacementFlags);
    if (VirtualBase == 0) {
        ERROR(" > failed to allocate virtual memory for the cloning of mappings");
        return OsError;
    }

    // Add required memory flags
    MemoryFlags |= (MAPPING_PERSISTENT | MAPPING_COMMIT);

    for (i = 0; i < PageCount; i++) {
        uintptr_t VirtualPage  = (VirtualBase + (i * GetMemorySpacePageSize()));
        uintptr_t PhysicalPage = DmaVector[i];
        assert(PhysicalPage != 0);
        
        Status = SetVirtualPageMapping(DestinationSpace, PhysicalPage, VirtualPage, MemoryFlags);
        // The only reason this ever turns error if the mapping exists, in this case free the allocated
        // resources if they are our allocations, and ignore
        if (Status != OsSuccess) {
            ERROR(" > failed to create virtual mapping for a clone mapping");
            break;
        }
    }
    return Status;
}

OsStatus_t
RemoveMemorySpaceMapping(
    _In_ SystemMemorySpace_t* SystemMemorySpace, 
    _In_ VirtualAddress_t     Address, 
    _In_ size_t               Size)
{
    OsStatus_t Status;
    int        PageCount = DIVUP(Size, GetMemorySpacePageSize());
    int        i;
    assert(SystemMemorySpace != NULL);

    // Free the underlying resources first, before freeing the upper resources
    for (i = 0; i < PageCount; i++) {
        uintptr_t VirtualPage = Address + (i * GetMemorySpacePageSize());
        Status                = ClearVirtualPageMapping(SystemMemorySpace, VirtualPage);
        if (Status != OsSuccess) {
            WARNING("Failed to unmap address 0x%" PRIxIN "", VirtualPage);
        }
    }
    SynchronizeMemoryRegion(SystemMemorySpace, Address, Size);

    // Free the range in either GAM or Process memory
    if (SystemMemorySpace->Context != NULL &&
        BlockBitmapValidateState(SystemMemorySpace->Context->HeapSpace, Address, 1) == OsSuccess) {
        ReleaseBlockmapRegion(SystemMemorySpace->Context->HeapSpace, Address, Size);
    }
    else if (BlockBitmapValidateState(&GetMachine()->GlobalAccessMemory, Address, 1) == OsSuccess) {
        ReleaseBlockmapRegion(&GetMachine()->GlobalAccessMemory, Address, Size);
    }
    else {
        // Ignore
    }
    return OsSuccess;
}

OsStatus_t
ChangeMemorySpaceProtection(
    _In_        SystemMemorySpace_t*    SystemMemorySpace,
    _InOut_Opt_ VirtualAddress_t        VirtualAddress, 
    _In_        size_t                  Size, 
    _In_        Flags_t                 Flags,
    _Out_       Flags_t*                PreviousFlags)
{
    OsStatus_t Status = OsSuccess;
    int        PageCount;
    int        i;
    assert(SystemMemorySpace != NULL);

    // Update the given pointer with previous flags, only flags from
    // the first page will be returned, so if flags vary this will be hidden.
    if (PreviousFlags != NULL) {
        Status = GetVirtualPageAttributes(SystemMemorySpace, VirtualAddress, PreviousFlags);
        if (Size == 0) {
            return OsSuccess;
        }
    }

    // Calculate the number of pages of this allocation
    PageCount = DIVUP((Size + (VirtualAddress % GetMemorySpacePageSize())), GetMemorySpacePageSize());
    for (i = 0; i < PageCount; i++) {
        uintptr_t Block = VirtualAddress + (i * GetMemorySpacePageSize());
        
        Status = SetVirtualPageAttributes(SystemMemorySpace, Block, Flags);
        if (Status != OsSuccess) {
            break;
        }
    }
    SynchronizeMemoryRegion(SystemMemorySpace, VirtualAddress, Size);
    return Status;
}

OsStatus_t
GetMemorySpaceMapping(
    _In_  SystemMemorySpace_t* MemorySpace, 
    _In_  VirtualAddress_t     Address,
    _In_  int                  PageCount,
    _Out_ uintptr_t*           DmaVectorOut)
{
    OsStatus_t Status   = OsSuccess;
    size_t     PageMask = GetMemorySpacePageSize() - 1; // only valid for 2^n
    int        i;
    
    assert(MemorySpace != NULL);
    assert(DmaVectorOut != NULL);
    
    // Behaviour we want here is only the first mapping to keep the offset
    for (i = 0; i < PageCount; i++, Address += GetMemorySpacePageSize()) {
        DmaVectorOut[i] = GetVirtualPageMapping(MemorySpace, Address);
        if (!i) {
            // The pagemask will be 0xFFF if the page size is 0x1000
            // so make sure we invert the bits
            Address &= ~(PageMask);
        }
    }
    return Status;
}

Flags_t
GetMemorySpaceAttributes(
    _In_ SystemMemorySpace_t* SystemMemorySpace, 
    _In_ VirtualAddress_t     VirtualAddress)
{
    Flags_t Attributes;
    assert(SystemMemorySpace != NULL);
    if (GetVirtualPageAttributes(SystemMemorySpace, VirtualAddress, &Attributes) != OsSuccess) {
        return 0;
    }
    return Attributes;
}

OsStatus_t
IsMemorySpacePageDirty(
    _In_ SystemMemorySpace_t*   SystemMemorySpace,
    _In_ VirtualAddress_t       Address)
{
    OsStatus_t Status = OsSuccess;
    Flags_t    Flags  = 0;
    
    // Sanitize address space
    assert(SystemMemorySpace != NULL);
    Status = GetVirtualPageAttributes(SystemMemorySpace, Address, &Flags);

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
    
    // Sanitize address space
    assert(SystemMemorySpace != NULL);
    Status = GetVirtualPageAttributes(SystemMemorySpace, Address, &Flags);

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
