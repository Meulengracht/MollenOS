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
 * MollenOS Memory Space Interface
 * - Implementation of virtual memory address spaces. This underlying
 *   hardware must support the __OSCONFIG_HAS_MMIO define to use this.
 */
#define __MODULE "MSPC"

#include <component/cpu.h>
#include <system/utils.h>
#include <memoryspace.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

// External functions, must be implemented in arch layer
extern OsStatus_t InitializeVirtualSpace(SystemMemorySpace_t*);
extern OsStatus_t CloneVirtualSpace(SystemMemorySpace_t*, SystemMemorySpace_t*, int);
extern OsStatus_t DestroyVirtualSpace(SystemMemorySpace_t*);
extern OsStatus_t SwitchVirtualSpace(SystemMemorySpace_t*);

extern OsStatus_t GetVirtualPageAttributes(SystemMemorySpace_t*, VirtualAddress_t, Flags_t*);
extern OsStatus_t SetVirtualPageAttributes(SystemMemorySpace_t*, VirtualAddress_t, Flags_t);

extern uintptr_t  GetVirtualPageMapping(SystemMemorySpace_t*, VirtualAddress_t);

extern OsStatus_t CommitVirtualPageMapping(SystemMemorySpace_t*, PhysicalAddress_t, VirtualAddress_t);
extern OsStatus_t SetVirtualPageMapping(SystemMemorySpace_t*, PhysicalAddress_t, VirtualAddress_t, Flags_t);
extern OsStatus_t ClearVirtualPageMapping(SystemMemorySpace_t*, VirtualAddress_t);
extern void       SynchronizePageRegion(SystemMemorySpace_t*, uintptr_t, size_t);

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
        *Handle = CreateHandle(HandleTypeMemorySpace, 0, MemorySpace);
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in CreateMemorySpace 0x%x", Flags);
    }
    return OsSuccess;
}

OsStatus_t
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
    return OsSuccess;
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

static PhysicalAddress_t
ResolvePhysicalMemorySpaceAddress(
    _In_  uintptr_t* PhysicalAddress,
    _In_  size_t     Size,
    _In_  Flags_t    PlacementFlags,
    _In_  uintptr_t  PhysicalMask,
    _Out_ int*       Cleanup)
{
    uintptr_t PhysicalBase = __MASK;
    
    switch (PlacementFlags & MAPPING_PHYSICAL_MASK) {
        case MAPPING_PHYSICAL_DEFAULT: {
            // Update on first allocation
            if (PhysicalAddress != NULL) {
                *PhysicalAddress = __MASK;
            }
        } break;

        case MAPPING_PHYSICAL_FIXED: {
            assert(PhysicalAddress != NULL);
            PhysicalBase = *PhysicalAddress;
        } break;
        
        case MAPPING_PHYSICAL_CONTIGIOUS: {
            PhysicalBase = AllocateSystemMemory(Size, PhysicalMask, 0); // MEMORY_DOMAIN
            assert(PhysicalBase != 0);
            if (PhysicalAddress != NULL) {
                *PhysicalAddress = PhysicalBase;
            }
            *Cleanup = 1;
        } break;

        default:
            assert(0);
            break;
    }
    return PhysicalBase;
}

static VirtualAddress_t
ResolveVirtualSystemMemorySpaceAddress(
    _In_ SystemMemorySpace_t* SystemMemorySpace,
    _In_ uintptr_t*           VirtualAddress,
    _In_ size_t               Size,
    _In_ Flags_t              PlacementFlags)
{
    VirtualAddress_t VirtualBase = 0;

    switch (PlacementFlags & MAPPING_VIRTUAL_MASK) {
        case MAPPING_VIRTUAL_FIXED: {
            assert(VirtualAddress != NULL);
            VirtualBase = *VirtualAddress;
        } break;

        case MAPPING_VIRTUAL_PROCESS: {
            assert(SystemMemorySpace->Context != NULL);
            VirtualBase = AllocateBlocksInBlockmap(SystemMemorySpace->Context->HeapSpace, __MASK, Size);
            if (VirtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%x (heap)", Size);
            }
        } break;

        case MAPPING_VIRTUAL_GLOBAL: {
            VirtualBase = AllocateBlocksInBlockmap(&GetMachine()->GlobalAccessMemory, __MASK, Size);
            if (VirtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%x (ga-memory)", Size);
            }
        } break;

        default: {
            FATAL(FATAL_SCOPE_KERNEL, "Failed to allocate virtual memory for flags: 0x%x", PlacementFlags);
        } break;
    }
    assert(VirtualBase != 0);

    if (VirtualAddress != NULL) {
        *VirtualAddress = VirtualBase;
    }
    return VirtualBase;
}

static OsStatus_t
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
            ERROR("Memory mapping at 0x%x already existed", VirtualAddress);
            assert((PlacementFlags & MAPPING_VIRTUAL_FIXED) != 0);
        }
    }
    return Status;
}

OsStatus_t
CreateMemorySpaceMapping(
    _In_        SystemMemorySpace_t* SystemMemorySpace,
    _InOut_Opt_ PhysicalAddress_t*   PhysicalAddress,
    _InOut_Opt_ VirtualAddress_t*    VirtualAddress,
    _In_        size_t               Size,
    _In_        Flags_t              MemoryFlags,
    _In_        Flags_t              PlacementFlags,
    _In_        uintptr_t            PhysicalMask)
{
    VirtualAddress_t  VirtualBase;
    PhysicalAddress_t PhysicalBase   = __MASK;
    OsStatus_t        Status         = OsError;
    int               PageCount      = DIVUP(Size, GetMemorySpacePageSize());
    int               CleanupOnError = 0;
    int               i;
    assert(SystemMemorySpace != NULL);
    assert(PlacementFlags != 0);

    // Handle the resolvement of the physical address, if physical-base is 0 after this
    // then we should allocate pages one-by-one as no requirements were set
    if (MemoryFlags & MAPPING_COMMIT) {
        PhysicalBase = ResolvePhysicalMemorySpaceAddress(PhysicalAddress, Size, 
            PlacementFlags, PhysicalMask, &CleanupOnError);
    }
    else if (PlacementFlags & MAPPING_PHYSICAL_FIXED) {
        assert(PhysicalAddress != NULL);
        PhysicalBase = *PhysicalAddress;
    }
    
    // Resolve the virtual address, if virtual-base is zero then we have trouble, as something
    // went wrong during the phase to figure out where to place
    VirtualBase = ResolveVirtualSystemMemorySpaceAddress(SystemMemorySpace, VirtualAddress, Size, PlacementFlags);
    if (VirtualBase != 0) {
        for (i = 0; i < PageCount; i++) {
            uintptr_t VirtualPage  = VirtualBase + (i * GetMemorySpacePageSize());
            uintptr_t PhysicalPage = 0;

            if (MemoryFlags & MAPPING_COMMIT) {
                if (PhysicalBase != __MASK) {
                    PhysicalPage = PhysicalBase + (i * GetMemorySpacePageSize());
                }
                else {
                    PhysicalPage = AllocateSystemMemory(GetMemorySpacePageSize(), PhysicalMask, 0);
                    assert(PhysicalPage != 0);
                    if (PhysicalAddress != NULL && *PhysicalAddress == __MASK) {
                        *PhysicalAddress = PhysicalPage;
                    }
                }
            }
            else if (PlacementFlags & MAPPING_PHYSICAL_FIXED) {
                PhysicalPage = PhysicalBase + (i * GetMemorySpacePageSize());
            }

            Status = InstallMemoryMapping(SystemMemorySpace, PhysicalPage, VirtualPage, MemoryFlags, PlacementFlags);
            if (Status != OsSuccess) {
                if (PhysicalBase == __MASK) {
                    FreeSystemMemory(PhysicalPage, GetMemorySpacePageSize());
                }
                break;
            }
        }

        // If we don't reach end of loop, should we undo?
        if (i != PageCount) {
            for (i -= 1; i >= 0; i--) {
                uintptr_t VirtualPage  = (VirtualBase + (i * GetMemorySpacePageSize()));
                ClearVirtualPageMapping(SystemMemorySpace, VirtualPage);
            }
        }
    }

    // Cleanup if we had preallocated space and we failed to map it in
    if (CleanupOnError && Status != OsSuccess) {
        FreeSystemMemory(PhysicalBase, Size);
    }
    return Status;
}

OsStatus_t
CommitMemorySpaceMapping(
    _In_        SystemMemorySpace_t* SystemMemorySpace,
    _InOut_Opt_ PhysicalAddress_t*   PhysicalAddress, 
    _In_        VirtualAddress_t     VirtualAddress,
    _In_        uintptr_t            PhysicalMask)
{
    uintptr_t  PhysicalPage;
    OsStatus_t Status = OsError;
    assert(SystemMemorySpace != NULL);

    PhysicalPage = AllocateSystemMemory(GetMemorySpacePageSize(), PhysicalMask, 0);
    assert(PhysicalPage != 0);
   
    Status = CommitVirtualPageMapping(SystemMemorySpace, PhysicalPage, VirtualAddress);
    if (Status != OsSuccess) {
        FreeSystemMemory(PhysicalPage, GetMemorySpacePageSize());
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
    OsStatus_t       Status    = OsSuccess;
    int              PageCount = DIVUP(Size, GetMemorySpacePageSize());
    int              i;
    assert(SourceSpace != NULL);
    assert(DestinationSpace != NULL);

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
        uintptr_t VirtualPage   = (VirtualBase + (i * GetMemorySpacePageSize()));
        uintptr_t PhysicalPage  = GetMemorySpaceMapping(SourceSpace, SourceAddress + (i * GetMemorySpacePageSize()));
        
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
        Status = ClearVirtualPageMapping(SystemMemorySpace, VirtualPage);
        if (Status != OsSuccess) {
            WARNING("Failed to unmap address 0x%x", VirtualPage);
        }
    }
    SynchronizePageRegion(SystemMemorySpace, Address, Size);

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
    SynchronizePageRegion(SystemMemorySpace, VirtualAddress, Size);
    return Status;
}

PhysicalAddress_t
GetMemorySpaceMapping(
    _In_ SystemMemorySpace_t*   SystemMemorySpace, 
    _In_ VirtualAddress_t       VirtualAddress)
{
    assert(SystemMemorySpace != NULL);
    return GetVirtualPageMapping(SystemMemorySpace, VirtualAddress);
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

/* IsMemorySpacePageDirty
 * Checks if the given virtual address is dirty (has been written data to). 
 * Returns OsSuccess if the address is dirty. */
OsStatus_t
IsMemorySpacePageDirty(
    _In_ SystemMemorySpace_t*   SystemMemorySpace,
    _In_ VirtualAddress_t       Address)
{
    // Variables
    OsStatus_t Status   = OsSuccess;
    Flags_t Flags       = 0;
    
    // Sanitize address space
    assert(SystemMemorySpace != NULL);
    Status = GetVirtualPageAttributes(SystemMemorySpace, Address, &Flags);

    // Check the flags if status was ok
    if (Status == OsSuccess && !(Flags & MAPPING_ISDIRTY)) {
        Status = OsError;
    }
    return Status;
}

/* IsMemorySpacePagePresent
 * Checks if the given virtual address is present. Returns success if the page
 * at the address has a mapping. */
OsStatus_t
IsMemorySpacePagePresent(
    _In_ SystemMemorySpace_t*   SystemMemorySpace,
    _In_ VirtualAddress_t       Address)
{
    // Variables
    OsStatus_t Status   = OsSuccess;
    Flags_t Flags       = 0;
    
    // Sanitize address space
    assert(SystemMemorySpace != NULL);
    Status = GetVirtualPageAttributes(SystemMemorySpace, Address, &Flags);

    // Check the flags if status was ok
    if (Status == OsSuccess && !(Flags & MAPPING_EXECUTABLE)) {
        Status = OsError;
    }
    return Status;
}

/* GetMemorySpacePageSize
 * Retrieves the memory page-size used by the underlying architecture. */
size_t
GetMemorySpacePageSize(void)
{
    return GetMachine()->MemoryGranularity;
}
