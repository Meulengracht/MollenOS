/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS Address Space Interface
 * - Contains the x86 implementation of the addressing interface
 *   specified by MCore
 */
#define __MODULE "ASPC"

/* Includes 
 * - System */
#include <system/addressspace.h>
#include <system/utils.h>
#include <threading.h>
#include <memory.h>
#include <debug.h>
#include <arch.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Globals */
static AddressSpace_t KernelAddressSpace    = { 0 };
static _Atomic(int) AddressSpaceIdGenerator = ATOMIC_VAR_INIT(1);

/* AddressSpaceInitialize
 * Initializes the Kernel Address Space. This only copies the data into a static global
 * storage, which means users should just pass something temporary structure */
OsStatus_t
AddressSpaceInitialize(
    _In_ AddressSpace_t *KernelSpace)
{
    // Variables
    int i;

	// Sanitize parameter
	assert(KernelSpace != NULL);

	// Copy data into our static storage
    for (i = 0; i < ASPACE_DATASIZE; i++) {
        KernelAddressSpace.Data[i] = KernelSpace->Data[i];
    }
	KernelAddressSpace.Flags = KernelSpace->Flags;

	// Setup reference and lock
	CriticalSectionConstruct(&KernelAddressSpace.SyncObject, CRITICALSECTION_REENTRANCY);
	atomic_store(&KernelAddressSpace.References, 1);
    KernelAddressSpace.Id           = atomic_fetch_add(&AddressSpaceIdGenerator, 1);
	return OsSuccess;
}

/* AddressSpaceCreate
 * Initialize a new address space, depending on what user is requesting we 
 * might recycle a already existing address space */
AddressSpace_t*
AddressSpaceCreate(
    _In_ Flags_t Flags)
{
	// Variables
	AddressSpace_t *AddressSpace    = NULL;

	// If we want to create a new kernel address
	// space we instead want to re-use the current 
	// If kernel is specified, ignore rest 
	if (Flags & ASPACE_TYPE_KERNEL) {
        atomic_fetch_add(&KernelAddressSpace.References, 1);
		AddressSpace = &KernelAddressSpace;
	}
	else if (Flags == ASPACE_TYPE_INHERIT) {
		// Inheritance is a bit different, we re-use again
		// but instead of reusing the kernel, we reuse the current
		AddressSpace    = AddressSpaceGetCurrent();
        atomic_fetch_add(&AddressSpace->References, 1);
	}
	else if (Flags & (ASPACE_TYPE_APPLICATION | ASPACE_TYPE_DRIVER)) {
		// This is the only case where we should create a 
		// new and seperate address space, user processes!
        uintptr_t DirectoryAddress  = 0;
        void *DirectoryPointer      = NULL;

		// Allocate a new address space
		AddressSpace        = (AddressSpace_t*)kmalloc(sizeof(AddressSpace_t));
        memset((void*)AddressSpace, 0, sizeof(AddressSpace_t));
        AddressSpace->Id    = atomic_fetch_add(&AddressSpaceIdGenerator, 1);
		AddressSpace->Flags = Flags;
		AddressSpace->References = 1;
		CriticalSectionConstruct(&AddressSpace->SyncObject, CRITICALSECTION_REENTRANCY);

        CriticalSectionEnter(&AddressSpaceGetCurrent()->SyncObject);
        MmVirtualClone((Flags & ASPACE_TYPE_INHERIT) ? 1 : 0, &DirectoryPointer, &DirectoryAddress);
        CriticalSectionLeave(&AddressSpaceGetCurrent()->SyncObject);
        assert(DirectoryPointer != NULL);
        assert(DirectoryAddress != 0);

		// Store new configuration into AS
        AddressSpace->Data[ASPACE_DATA_PDPOINTER]   = (uintptr_t)DirectoryPointer;
        AddressSpace->Data[ASPACE_DATA_CR3]         = DirectoryAddress;
	}
	else {
		FATAL(FATAL_SCOPE_KERNEL, "Invalid flags parsed in AddressSpaceCreate 0x%x", Flags);
	}
	return AddressSpace;
}

/* AddressSpaceDestroy
 * Destroy and release all resources related to an address space, 
 * only if there is no more references */
OsStatus_t
AddressSpaceDestroy(
    _In_ AddressSpace_t *AddressSpace)
{
	// Acquire lock on the address space
    int References = atomic_fetch_sub(&AddressSpace->References, 1);

	// In case that was the last reference cleanup the address space otherwise
	// just unlock
	if ((References - 1) == 0) {
		if (AddressSpace->Flags & (ASPACE_TYPE_APPLICATION | ASPACE_TYPE_DRIVER)) {
			MmVirtualDestroy((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER]);
		}
		kfree(AddressSpace);
	}
	return OsSuccess;
}

/* AddressSpaceSwitch
 * Switches the current address space out with the the address space provided 
 * for the current cpu */
OsStatus_t
AddressSpaceSwitch(
    _In_ AddressSpace_t *AddressSpace) {
	return UpdateVirtualAddressingSpace(
        (void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], 
        AddressSpace->Data[ASPACE_DATA_CR3]);
}

/* AddressSpaceGetCurrent
 * Returns the current address space if there is no active threads or threading
 * is not setup it returns the kernel address space */
AddressSpace_t*
AddressSpaceGetCurrent(void)
{
	// Lookup current thread
	MCoreThread_t *CurrentThread = ThreadingGetCurrentThread(CpuGetCurrentId());

	// if no threads are active return the kernel address space
	if (CurrentThread == NULL) {
		return &KernelAddressSpace;
	}
	else {
        assert(CurrentThread->AddressSpace != NULL);
		return CurrentThread->AddressSpace;
	}
}

/* AddressSpaceGetNativeFlags
 * Converts address-space generic flags to native page flags */
Flags_t
AddressSpaceGetNativeFlags(Flags_t Flags)
{
    // Variables
    Flags_t NativeFlags = PAGE_PRESENT;
    if (Flags & ASPACE_FLAG_APPLICATION) {
		NativeFlags |= PAGE_USER;
	}
	if (Flags & ASPACE_FLAG_NOCACHE) {
		NativeFlags |= PAGE_CACHE_DISABLE;
	}
	if (Flags & ASPACE_FLAG_VIRTUAL) {
		NativeFlags |= PAGE_VIRTUAL;
	}
    if (!(Flags & ASPACE_FLAG_READONLY)) {
        NativeFlags |= PAGE_WRITE;
    }
    return NativeFlags;
}

/* AddressSpaceChangeProtection
 * Changes the protection parameters for the given memory region.
 * The region must already be mapped and the size will be rounded up
 * to a multiple of the page-size. */
OsStatus_t
AddressSpaceChangeProtection(
    _In_        AddressSpace_t*     AddressSpace,
    _InOut_Opt_ VirtualAddress_t    VirtualAddress,
    _In_        size_t              Size,
    _In_        Flags_t             Flags,
    _Out_       Flags_t*            PreviousFlags)
{
    // Variables
	Flags_t ProtectionFlags         = AddressSpaceGetNativeFlags(Flags);
    OsStatus_t Result               = OsSuccess;
    int PageCount                   = 0;
    int i;

    // Assert that address space is not null
    assert(AddressSpace != NULL);

    // Calculate the number of pages of this allocation
    PageCount           = DIVUP((Size + VirtualAddress & ATTRIBUTE_MASK), PAGE_SIZE);

    // Update pages with new protection
    CriticalSectionEnter(&AddressSpace->SyncObject);
    for (i = 0; i < PageCount; i++) {
        uintptr_t Block = VirtualAddress + (i * PAGE_SIZE);
        if (PreviousFlags != NULL) {
            MmVirtualGetFlags((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], Block, PreviousFlags);
        }
        if (MmVirtualSetFlags((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], Block, ProtectionFlags) != OsSuccess) {
            Result = OsError;
            break;
        }
    }
    CriticalSectionLeave(&AddressSpace->SyncObject);
    return Result;
}

/* AddressSpaceMap
 * Maps the given virtual address into the given address space
 * uses the given physical pages instead of automatic allocation
 * It returns the start address of the allocated physical region */
OsStatus_t
AddressSpaceMap(
    _In_        AddressSpace_t*     AddressSpace,
    _InOut_Opt_ PhysicalAddress_t*  PhysicalAddress, 
    _InOut_Opt_ VirtualAddress_t*   VirtualAddress, 
    _In_        size_t              Size, 
    _In_        Flags_t             Flags,
    _In_        uintptr_t           Mask)
{
    // Variables
	PhysicalAddress_t PhysicalBase  = 0;
    VirtualAddress_t VirtualBase    = 0;
    OsStatus_t Status               = OsSuccess;
	Flags_t AllocFlags              = 0;
	int PageCount                   = 0;
	int i;

    // Assert that address space is not null
    assert(AddressSpace != NULL);

    // Calculate the number of pages of this allocation
    PageCount           = DIVUP(Size, PAGE_SIZE);
    
    // Make sure the address is not already mapped, this can happen when multiple
    // cpu's allocate addresses that are alike at the same time
    CriticalSectionEnter(&AddressSpace->SyncObject);
    if (Flags & ASPACE_FLAG_SUPPLIEDVIRTUAL) {
        PhysicalBase = MmVirtualGetMapping((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], *VirtualAddress);
        if (PhysicalBase != 0) {
            // The address is already mapped, ignore this
            if (Flags & ASPACE_FLAG_VIRTUAL) {
                if ((*PhysicalAddress & PAGE_MASK) != (PhysicalBase & PAGE_MASK)) {
                    FATAL(FATAL_SCOPE_KERNEL, "Tried to remap fixed virtual address from different physical");
                }
            }
            goto Cleanup;
        }
    }

    // Determine the memory mappings initially
    if (Flags & ASPACE_FLAG_VIRTUAL) {
        assert(PhysicalAddress != NULL);
        PhysicalBase        = (*PhysicalAddress & PAGE_MASK);
    }
    else if (Flags & ASPACE_FLAG_CONTIGIOUS) { // Allocate contigious physical? 
        PhysicalBase = MmPhysicalAllocateBlock(Mask, PageCount);
        if (PhysicalAddress != NULL) {
            *PhysicalAddress = PhysicalBase;
        }
    }
    else { // Set it on first allocation
        if (PhysicalAddress != NULL) {
            *PhysicalAddress = 0;
        }
    }
    if (Flags & ASPACE_FLAG_SUPPLIEDVIRTUAL) {
        assert(VirtualAddress != NULL);
        VirtualBase         = (*VirtualAddress & PAGE_MASK);
    }
    else { // allocate from some place... @todo
        VirtualBase         = (VirtualAddress_t)MmReserveMemory(PageCount);
        if (VirtualAddress != NULL) {
            *VirtualAddress = VirtualBase;
        }
    }

    // Handle other flags
    AllocFlags = AddressSpaceGetNativeFlags(Flags);

    // Iterate the number of pages to map 
	for (i = 0; i < PageCount; i++) {
		uintptr_t PhysicalPage  = 0;
        if ((Flags & ASPACE_FLAG_CONTIGIOUS) || (Flags & ASPACE_FLAG_VIRTUAL)) {
            PhysicalPage        = PhysicalBase + (i * PAGE_SIZE);
        }
		else {
			PhysicalPage        = MmPhysicalAllocateBlock(Mask, 1);
            if (PhysicalAddress != NULL && *PhysicalAddress == 0) {
                *PhysicalAddress = PhysicalPage;
            }
		}

		// Redirect call to our virtual page manager
		if (MmVirtualMap((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], 
            PhysicalPage, (VirtualBase + (i * PAGE_SIZE)), AllocFlags) != OsSuccess) {
            WARNING("Failed to map virtual 0x%x => physical 0x%x", (VirtualBase + (i * PAGE_SIZE)), PhysicalPage);
			Status = OsError;
            break;
		}
	}
    
Cleanup:
    CriticalSectionLeave(&AddressSpace->SyncObject);
    return Status;
}

/* AddressSpaceUnmap
 * Unmaps a virtual memory region from an address space */
OsStatus_t
AddressSpaceUnmap(
    _In_ AddressSpace_t*    AddressSpace, 
    _In_ VirtualAddress_t   Address, 
    _In_ size_t             Size)
{
	// Variables
	int PageCount   = DIVUP(Size, PAGE_SIZE);
	int i;

    // Sanitize address space
    assert(AddressSpace != NULL);

	// Iterate page-count and unmap
    CriticalSectionEnter(&AddressSpace->SyncObject);
	for (i = 0; i < PageCount; i++) {
        if (MmVirtualGetMapping((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], (Address + (i * PAGE_SIZE))) != 0) {
            if (MmVirtualUnmap((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], (Address + (i * PAGE_SIZE))) != OsSuccess) {
                WARNING("Failed to unmap address 0x%x", (Address + (i * PAGE_SIZE)));
            }
        }
        else {
            TRACE("Ignoring free on unmapped address 0x%x", (Address + (i * PAGE_SIZE)));
        }
	}
    CriticalSectionLeave(&AddressSpace->SyncObject);
	return OsSuccess;
}

/* AddressSpaceGetMapping
 * Retrieves a physical mapping from an address space determined
 * by the virtual address given */
PhysicalAddress_t
AddressSpaceGetMapping(
    _In_ AddressSpace_t*    AddressSpace, 
    _In_ VirtualAddress_t   VirtualAddress)
{
    // Variables
    PhysicalAddress_t MappedAddress = 0;

    // Sanitize address space
    assert(AddressSpace != NULL);
    CriticalSectionEnter(&AddressSpace->SyncObject);
	MappedAddress = MmVirtualGetMapping((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], VirtualAddress);
    CriticalSectionLeave(&AddressSpace->SyncObject);
    return MappedAddress;
}

/* AddressSpaceIsDirty
 * Checks if the given virtual address is dirty (has been written data to). 
 * Returns OsSuccess if the address is dirty. */
OsStatus_t
AddressSpaceIsDirty(
    _In_ AddressSpace_t*    AddressSpace,
    _In_ VirtualAddress_t   Address)
{
    OsStatus_t Status   = OsSuccess;
    Flags_t Flags       = 0;
    
    // Sanitize address space
    assert(AddressSpace != NULL);

    CriticalSectionEnter(&AddressSpace->SyncObject);
    Status = MmVirtualGetFlags((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], Address, &Flags);
    CriticalSectionLeave(&AddressSpace->SyncObject);

    // Check the flags if status was ok
    if (Status == OsSuccess && !(Flags & PAGE_DIRTY)) {
        Status = OsError;
    }
    return Status;
}

/* AddressSpaceGetPageSize
 * Retrieves the memory page-size used by the underlying architecture. */
size_t
AddressSpaceGetPageSize(void) {
    return PAGE_SIZE;
}
