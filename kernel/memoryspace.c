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

#include <process/phoenix.h>
#include <component/cpu.h>
#include <system/utils.h>
#include <memoryspace.h>
#include <threading.h>
#include <machine.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>

// External functions, must be implemented in arch layer
extern OsStatus_t   InitializeVirtualSpace(SystemMemorySpace_t*);
extern OsStatus_t   CloneVirtualSpace(SystemMemorySpace_t*, SystemMemorySpace_t*, int);
extern OsStatus_t   DestroyVirtualSpace(SystemMemorySpace_t*);
extern OsStatus_t   SwitchVirtualSpace(SystemMemorySpace_t*);
extern OsStatus_t   ResolveVirtualSpaceAddress(SystemMemorySpace_t*, size_t, Flags_t, VirtualAddress_t*);

extern OsStatus_t   GetVirtualPageAttributes(SystemMemorySpace_t*, VirtualAddress_t, Flags_t*);
extern OsStatus_t   SetVirtualPageAttributes(SystemMemorySpace_t*, VirtualAddress_t, Flags_t);

extern uintptr_t    GetVirtualPageMapping(SystemMemorySpace_t*, VirtualAddress_t);
extern OsStatus_t   SetVirtualPageMapping(SystemMemorySpace_t*, PhysicalAddress_t, VirtualAddress_t, Flags_t);
extern OsStatus_t   ClearVirtualPageMapping(SystemMemorySpace_t*, VirtualAddress_t);
extern void         SynchronizePageRegion(SystemMemorySpace_t*, uintptr_t, size_t);

// Global static storage
static _Atomic(int) AddressSpaceIdGenerator = ATOMIC_VAR_INIT(1);

/* InitializeSystemMemorySpace
 * Initializes the system memory space. This initializes a static version of the
 * system memory space which is the default space the cpu should use for kernel operation. */
OsStatus_t
InitializeSystemMemorySpace(
    _In_ SystemMemorySpace_t*   SystemMemorySpace)
{
    // Setup reference and lock
    SystemMemorySpace->Parent   = NULL;
    SystemMemorySpace->Id       = atomic_fetch_add(&AddressSpaceIdGenerator, 1);
    CriticalSectionConstruct(&SystemMemorySpace->SyncObject, CRITICALSECTION_REENTRANCY);
    atomic_store(&SystemMemorySpace->References, 1);

    // Let the architecture initialize further
    return InitializeVirtualSpace(SystemMemorySpace);
}

/* CreateSystemMemorySpace
 * Initialize a new memory space, depending on what user is requesting we 
 * might recycle a already existing address space */
SystemMemorySpace_t*
CreateSystemMemorySpace(
    _In_ Flags_t                Flags)
{
    SystemMemorySpace_t *MemorySpace = NULL;
    int i;

    // If we want to create a new kernel address
    // space we instead want to re-use the current 
    // If kernel is specified, ignore rest 
    if (Flags == MEMORY_SPACE_INHERIT) {
        // Inheritance is a bit different, we re-use again
        // but instead of reusing the kernel, we reuse the current
        MemorySpace = GetCurrentSystemMemorySpace();
        atomic_fetch_add(&MemorySpace->References, 1);
    }
    else if (Flags & (MEMORY_SPACE_APPLICATION | MEMORY_SPACE_SERVICE)) {
        // Allocate a new address space
        MemorySpace = (SystemMemorySpace_t*)kmalloc(sizeof(SystemMemorySpace_t));
        memset((void*)MemorySpace, 0, sizeof(SystemMemorySpace_t));

        MemorySpace->Id         = atomic_fetch_add(&AddressSpaceIdGenerator, 1);
        MemorySpace->Flags      = Flags;
        MemorySpace->References = 1;
        CriticalSectionConstruct(&MemorySpace->SyncObject, CRITICALSECTION_REENTRANCY);

        // Parent must be the upper-most instance of the address-space
        // of the process. Only to the point of not having kernel as parent
        MemorySpace->Parent    = (GetCurrentSystemMemorySpace()->Parent != NULL) ? 
            GetCurrentSystemMemorySpace()->Parent : GetCurrentSystemMemorySpace();
        if (MemorySpace->Parent == GetSystemMemorySpace()) {
            MemorySpace->Parent = NULL;
        }
        
        // If we have a parent, both add a new reference to the parent
        // and also copy all its members. 
        if (MemorySpace->Parent != NULL) {
            atomic_fetch_add(&MemorySpace->Parent->References, 1);
            for (i = 0; i < MEMORY_DATACOUNT; i++) {
                MemorySpace->Data[i] = MemorySpace->Parent->Data[i];
            }
        }
        CloneVirtualSpace(MemorySpace->Parent, MemorySpace, (Flags & MEMORY_SPACE_INHERIT) ? 1 : 0);
    }
    else {
        FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in CreateSystemMemorySpace 0x%x", Flags);
    }
    return MemorySpace;
}

/* ReleaseSystemMemorySpace
 * Destroy and release all resources related to an address space, 
 * only if there is no more references */
OsStatus_t
ReleaseSystemMemorySpace(
    _In_ SystemMemorySpace_t*   SystemMemorySpace)
{
    // Acquire lock on the address space
    int References = atomic_fetch_sub(&SystemMemorySpace->References, 1) - 1;

    // In case that was the last reference cleanup the address space otherwise
    // just unlock
    if (References == 0) {
        if (SystemMemorySpace->Flags & (MEMORY_SPACE_APPLICATION | MEMORY_SPACE_SERVICE)) {
            DestroyVirtualSpace(SystemMemorySpace);
        }
        kfree(SystemMemorySpace);
    }

    // Reduce a reference to our parent as-well if we have one
    return (SystemMemorySpace->Parent == NULL) ? OsSuccess : ReleaseSystemMemorySpace(SystemMemorySpace->Parent);
}

/* SwitchSystemMemorySpace
 * Switches the current address space out with the the address space provided 
 * for the current cpu */
OsStatus_t
SwitchSystemMemorySpace(
    _In_ SystemMemorySpace_t*   SystemMemorySpace)
{
    return SwitchVirtualSpace(SystemMemorySpace);
}

/* GetCurrentSystemMemorySpace
 * Returns the current address space if there is no active threads or threading
 * is not setup it returns the kernel address space */
SystemMemorySpace_t*
GetCurrentSystemMemorySpace(void)
{
    // Lookup current thread
    MCoreThread_t *CurrentThread = ThreadingGetCurrentThread(CpuGetCurrentId());

    // if no threads are active return the kernel address space
    if (CurrentThread == NULL) {
        return GetSystemMemorySpace();
    }
    else {
        assert(CurrentThread->MemorySpace != NULL);
        return CurrentThread->MemorySpace;
    }
}

/* GetSystemMemorySpace
 * Retrieves the system's current copy of its memory space. If domains are active it will
 * be for the current domain, if system is uma-mode it's the machine wide. */
SystemMemorySpace_t*
GetSystemMemorySpace(void)
{
    return (GetCurrentDomain() != NULL) ? &GetCurrentDomain()->SystemSpace : &GetMachine()->SystemSpace;
}

/* ChangeSystemMemorySpaceProtection
 * Changes the protection parameters for the given memory region.
 * The region must already be mapped and the size will be rounded up
 * to a multiple of the page-size. */
OsStatus_t
ChangeSystemMemorySpaceProtection(
    _In_        SystemMemorySpace_t*    SystemMemorySpace,
    _InOut_Opt_ VirtualAddress_t        VirtualAddress, 
    _In_        size_t                  Size, 
    _In_        Flags_t                 Flags,
    _Out_       Flags_t*                PreviousFlags)
{
    // Variables
    OsStatus_t Status = OsSuccess;
    int PageCount;
    int i;

    // Assert that address space is not null
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
    PageCount = DIVUP((Size + (VirtualAddress % GetSystemMemoryPageSize())), GetSystemMemoryPageSize());

    // Update pages with new protection
    for (i = 0; i < PageCount; i++) {
        uintptr_t Block = VirtualAddress + (i * GetSystemMemoryPageSize());
        
        Status = SetVirtualPageAttributes(SystemMemorySpace, Block, Flags);
        if (Status != OsSuccess) {
            break;
        }
    }
    SynchronizePageRegion(SystemMemorySpace, VirtualAddress, Size);
    return Status;
}

/* ResolvePhysicalMemorySpaceAddress
 * Resolves the physical memory address that should be used for the mapping
 * based on the mapping flags. */
PhysicalAddress_t
ResolvePhysicalMemorySpaceAddress(
    _In_ size_t                 Size,
    _In_ uintptr_t              Mask,
    _In_ Flags_t                Flags)
{
    uintptr_t PhysicalBase = 0;
    switch (Flags & MAPPING_PMODE_MASK) {
        case MAPPING_CONTIGIOUS: {
            Flags_t PhysicalMemoryFlags = (Flags & MAPPING_DOMAIN) ? MEMORY_DOMAIN : 0;
            PhysicalBase                = AllocateSystemMemory(Size, Mask, PhysicalMemoryFlags);
            if (PhysicalBase == 0) {
                ERROR(" > failed to allocate contiguous memory, soon out of memory!");
                break;
            }
        } break;

        default:
            break;
    }
    return PhysicalBase;
}

/* ResolveVirtualSystemMemorySpaceAddress
 * Resolves the virtual memory address that shuld be used for mapping the
 * requested memory. */
VirtualAddress_t
ResolveVirtualSystemMemorySpaceAddress(
    _In_ SystemMemorySpace_t*   SystemMemorySpace,
    _In_ size_t                 Size,
    _In_ uintptr_t              Mask,
    _In_ Flags_t                Flags)
{
    VirtualAddress_t VirtualBase = 0;
    switch (Flags & MAPPING_VMODE_MASK) {
        case MAPPING_PROCESS: {
            MCoreAsh_t *CurrentProcess = PhoenixGetCurrentAsh();
            assert(CurrentProcess != NULL);
            VirtualBase = AllocateBlocksInBlockmap(CurrentProcess->Heap, Mask, Size);
            if (VirtualBase == 0) {
                ERROR("Ran out of memory for allocation 0x%x (heap)", Size);
                break;
            }
        } break;

        default: {
            // Let the platfrom resolve these
            OsStatus_t Status = ResolveVirtualSpaceAddress(SystemMemorySpace, Size, Flags, &VirtualBase);
            if (Status == OsSuccess) {
                break;
            }
        } break;
    }
    return VirtualBase;
}

/* CreateSystemMemorySpaceMapping
 * Maps the given virtual address into the given address space
 * uses the given physical pages instead of automatic allocation
 * It returns the start address of the allocated physical region */
OsStatus_t
CreateSystemMemorySpaceMapping(
    _In_        SystemMemorySpace_t*    SystemMemorySpace,
    _InOut_Opt_ PhysicalAddress_t*      PhysicalAddress,
    _InOut_Opt_ VirtualAddress_t*       VirtualAddress, 
    _In_        size_t                  Size, 
    _In_        Flags_t                 Flags,
    _In_        uintptr_t               Mask)
{
    PhysicalAddress_t PhysicalBase;
    VirtualAddress_t VirtualBase;
    OsStatus_t Status               = OsSuccess;
    int PageCount                   = DIVUP(Size, GetSystemMemoryPageSize());
    int i;
    assert(SystemMemorySpace != NULL);

    // Get the physical address base, however it can turn out to be 0. This simply
    // means we handle it one at the time in the mapping process
    if (Flags & MAPPING_PROVIDED) { 
        assert(PhysicalAddress != NULL);
        PhysicalBase = *PhysicalAddress;
    }
    else {
        PhysicalBase = ResolvePhysicalMemorySpaceAddress(Size, Mask, Flags);
        if (PhysicalAddress != NULL) {
            *PhysicalAddress = 0; // Reset it and update on first map
        }
    }

    // Get the virtual address space, this however may not end up as 0 if it the mapping
    // is not provided already.
    if (Flags & MAPPING_FIXED) {
        assert(VirtualAddress != NULL);
        VirtualBase = *VirtualAddress;
    }
    else {
        VirtualBase = ResolveVirtualSystemMemorySpaceAddress(SystemMemorySpace, Size, Mask, Flags);
        if (VirtualBase == 0) {
            ERROR(" > failed to allocate virtual memory for the mapping");
            return OsError;
        }
        if (VirtualAddress != NULL) {
            *VirtualAddress = VirtualBase;
        }
    }

    // Iterate the number of pages to map 
    for (i = 0; i < PageCount; i++) {
        uintptr_t VirtualPage   = (VirtualBase + (i * GetSystemMemoryPageSize()));
        uintptr_t PhysicalPage  = 0;
        
        if (PhysicalBase != 0 || (Flags & MAPPING_PROVIDED)) {
            PhysicalPage        = PhysicalBase + (i * GetSystemMemoryPageSize());
        }
        else {
            PhysicalPage        = AllocateSystemMemory(GetSystemMemoryPageSize(), Mask, 0);  // MAPPING_DOMAIN
            if (PhysicalAddress != NULL && *PhysicalAddress == 0) {
                *PhysicalAddress = PhysicalPage;
            }
        }

        Status = SetVirtualPageMapping(SystemMemorySpace, PhysicalPage, VirtualPage, Flags);
        // The only reason this ever turns error if the mapping exists, in this case free the allocated
        // resources if they are our allocations, and ignore
        if (Status != OsSuccess) {
            if ((Flags & MAPPING_CONTIGIOUS) && i != 0) {
                FATAL(FATAL_SCOPE_KERNEL, "Remapping error with a contigious call");
            }

            // Never unmap fixed-physical pages, this is important
            if (!(Flags & MAPPING_PROVIDED)) {
                FreeSystemMemory(PhysicalPage, GetSystemMemoryPageSize());
            }

            // In case of <already-mapped> we might want to update the reference
            if (PhysicalAddress != NULL && *PhysicalAddress == PhysicalPage) {
                *PhysicalAddress = GetVirtualPageMapping(SystemMemorySpace, VirtualPage);
            }
        }
    }
    return Status;
}

/* CloneSystemMemorySpaceMapping
 * Clones a region of memory mappings into the address space provided. The new mapping
 * will automatically be marked PERSISTANT and PROVIDED. */
OsStatus_t
CloneSystemMemorySpaceMapping(
    _In_        SystemMemorySpace_t*    SourceSpace,
    _In_        SystemMemorySpace_t*    DestinationSpace,
    _In_        VirtualAddress_t        SourceAddress,
    _InOut_Opt_ VirtualAddress_t*       DestinationAddress,
    _In_        size_t                  Size, 
    _In_        Flags_t                 Flags,
    _In_        uintptr_t               Mask)
{
    VirtualAddress_t VirtualBase;
    OsStatus_t Status               = OsSuccess;
    int PageCount                   = DIVUP(Size, GetSystemMemoryPageSize());
    int i;
    assert(SourceSpace != NULL);
    assert(DestinationSpace != NULL);

    // Get the virtual address space, this however may not end up as 0 if it the mapping
    // is not provided already.
    if (Flags & MAPPING_FIXED) {
        assert(DestinationAddress != NULL);
        VirtualBase = *DestinationAddress;
    }
    else {
        VirtualBase = ResolveVirtualSystemMemorySpaceAddress(DestinationSpace, Size, Mask, 
            Flags | MAPPING_PERSISTENT | MAPPING_PROVIDED);
        if (VirtualBase == 0) {
            ERROR(" > failed to allocate virtual memory for the mapping");
            return OsError;
        }
        if (DestinationAddress != NULL) {
            *DestinationAddress = VirtualBase;
        }
    }

    // Iterate the number of pages to map 
    for (i = 0; i < PageCount; i++) {
        uintptr_t VirtualPage   = (VirtualBase + (i * GetSystemMemoryPageSize()));
        uintptr_t PhysicalPage  = GetSystemMemoryMapping(SourceSpace, SourceAddress + (i * GetSystemMemoryPageSize()));

        WARNING(" > mapping 0x%x (phys) to 0x%x (virt)", PhysicalPage, VirtualPage);
        Status = SetVirtualPageMapping(DestinationSpace, PhysicalPage, VirtualPage, 
            Flags | MAPPING_PERSISTENT | MAPPING_PROVIDED);
        // The only reason this ever turns error if the mapping exists, in this case free the allocated
        // resources if they are our allocations, and ignore
        if (Status != OsSuccess) {
            ERROR(" > failed to create virtual mapping for a clone mapping");
            break;
        }
    }
    return Status;
}

/* RemoveSystemMemoryMapping
 * Unmaps a virtual memory region from an address space */
OsStatus_t
RemoveSystemMemoryMapping(
    _In_ SystemMemorySpace_t*   SystemMemorySpace, 
    _In_ VirtualAddress_t       Address, 
    _In_ size_t                 Size)
{
    OsStatus_t Status;
    int PageCount = DIVUP(Size, GetSystemMemoryPageSize());
    int i;

    // Sanitize address space
    assert(SystemMemorySpace != NULL);

    for (i = 0; i < PageCount; i++) {
        uintptr_t VirtualPage = Address + (i * GetSystemMemoryPageSize());
        if (GetVirtualPageMapping(SystemMemorySpace, VirtualPage) != 0) {
            Status = ClearVirtualPageMapping(SystemMemorySpace, VirtualPage);
            if (Status != OsSuccess) {
                WARNING("Failed to unmap address 0x%x", VirtualPage);
            }
        }
        else {
            TRACE("Ignoring free on unmapped address 0x%x", VirtualPage);
        }
    }
    SynchronizePageRegion(SystemMemorySpace, Address, Size);
    return OsSuccess;
}

/* GetSystemMemoryMapping
 * Retrieves a physical mapping from an address space determined
 * by the virtual address given */
PhysicalAddress_t
GetSystemMemoryMapping(
    _In_ SystemMemorySpace_t*   SystemMemorySpace, 
    _In_ VirtualAddress_t       VirtualAddress)
{
    assert(SystemMemorySpace != NULL);
    return GetVirtualPageMapping(SystemMemorySpace, VirtualAddress);
}

/* IsSystemMemoryPageDirty
 * Checks if the given virtual address is dirty (has been written data to). 
 * Returns OsSuccess if the address is dirty. */
OsStatus_t
IsSystemMemoryPageDirty(
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

/* IsSystemMemoryPresent
 * Checks if the given virtual address is present. Returns success if the page
 * at the address has a mapping. */
OsStatus_t
IsSystemMemoryPresent(
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

/* GetSystemMemoryPageSize
 * Retrieves the memory page-size used by the underlying architecture. */
size_t
GetSystemMemoryPageSize(void)
{
    return GetMachine()->MemoryGranularity;
}
