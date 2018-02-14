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
 * - Contains the x86-32 implementation of the addressing interface
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
static UUId_t AddressSpaceIdGenerator       = 0;

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
    AddressSpaceIdGenerator = 0;

	// Copy data into our static storage
    for (i = 0; i < ASPACE_DATASIZE; i++) {
        KernelAddressSpace.Data[i] = KernelSpace->Data[i];
    }
	KernelAddressSpace.Flags = KernelSpace->Flags;

	// Setup reference and lock
	SpinlockReset(&KernelAddressSpace.Lock);
	KernelAddressSpace.References   = 1;
    KernelAddressSpace.Id           = AddressSpaceIdGenerator++;
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
	UUId_t CurrentCpu               = CpuGetCurrentId();
	int Itr                         = 0;

	// If we want to create a new kernel address
	// space we instead want to re-use the current 
	// If kernel is specified, ignore rest 
	if (Flags & ASPACE_TYPE_KERNEL) {
		SpinlockAcquire(&KernelAddressSpace.Lock);
		KernelAddressSpace.References++;
		SpinlockRelease(&KernelAddressSpace.Lock);
		AddressSpace = &KernelAddressSpace;
	}
	else if (Flags == ASPACE_TYPE_INHERIT) {
		// Inheritance is a bit different, we re-use again
		// but instead of reusing the kernel, we reuse the current
		MCoreThread_t *Current = ThreadingGetCurrentThread(CurrentCpu);
		SpinlockAcquire(&Current->AddressSpace->Lock);
		Current->AddressSpace->References++;
		SpinlockRelease(&Current->AddressSpace->Lock);
		AddressSpace = Current->AddressSpace;
	}
	else if (Flags & (ASPACE_TYPE_APPLICATION | ASPACE_TYPE_DRIVER)) {
		// This is the only case where we should create a 
		// new and seperate address space, user processes!
		uintptr_t PhysicalAddress   = 0;
		PageDirectory_t *NewPd      = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &PhysicalAddress);
		PageDirectory_t *CurrPd     = (PageDirectory_t*)AddressSpaceGetCurrent()->Data[ASPACE_DATA_PDPOINTER];
		PageDirectory_t *KernPd     = (PageDirectory_t*)KernelAddressSpace.Data[ASPACE_DATA_PDPOINTER];

		// Copy at max kernel directories up to MEMORY_SEGMENT_RING3_BASE
		int KernelRegion            = 0;
		int KernelRegionEnd         = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_KERNEL_END);

		// Lookup which table-region is the stack region
		int ThreadRegion            = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
		int ThreadRegionEnd         = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_END);

		// Allocate a new address space
		AddressSpace                = (AddressSpace_t*)kmalloc(sizeof(AddressSpace_t));

		// Initialize members
		memset(NewPd, 0, sizeof(PageDirectory_t));
		MutexConstruct(&NewPd->Lock);

		// Initialize base mappings
		for (Itr = 0; Itr < TABLES_PER_PDIR; Itr++) {
			// Sanitize if it's inside kernel region
			if (Itr >= KernelRegion && Itr < KernelRegionEnd) {
				NewPd->pTables[Itr] = KernPd->pTables[Itr];
				NewPd->vTables[Itr] = KernPd->vTables[Itr];
				continue;
			}

			// Sanitize stack region, never copy
			if (Itr >= ThreadRegion && Itr <= ThreadRegionEnd) {
				continue;
			}

			// Inherit? We must mark that table inherited to avoid
			// it being freed again
			if ((Flags & ASPACE_TYPE_INHERIT) && CurrPd->pTables[Itr]) {
				NewPd->pTables[Itr] = CurrPd->pTables[Itr] | PAGE_INHERITED;
				NewPd->vTables[Itr] = CurrPd->vTables[Itr];
			}
		}

		// Store new configuration into AS
        AddressSpace->Id                            = AddressSpaceIdGenerator++;
		AddressSpace->Flags                         = Flags;
        AddressSpace->Data[ASPACE_DATA_CR3]         = PhysicalAddress;
        AddressSpace->Data[ASPACE_DATA_PDPOINTER]   = (uintptr_t)NewPd;
		AddressSpace->References                    = 1;
		SpinlockReset(&AddressSpace->Lock);
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
	SpinlockAcquire(&AddressSpace->Lock);
	AddressSpace->References--;

	// In case that was the last reference
	// cleanup the address space otherwise
	// just unlock
	if (AddressSpace->References == 0) {
		if (AddressSpace->Flags & (ASPACE_TYPE_APPLICATION | ASPACE_TYPE_DRIVER)) {
			PageDirectory_t *KernPd = (PageDirectory_t*)KernelAddressSpace.Data[ASPACE_DATA_PDPOINTER];
			PageDirectory_t *Pd = (PageDirectory_t*)AddressSpace->Data[ASPACE_DATA_PDPOINTER];
			int i, j;

			// Iterate page-mappings
			for (i = 0; i < TABLES_PER_PDIR; i++) {
				if (Pd->pTables[i] == 0)
					continue;

				// Is it a kernel page-table? Ignore it 
				if (Pd->pTables[i] == KernPd->pTables[i])
					continue;

				// Is this an inherited page-table?
				// We don't free our parents stuff
				if (Pd->pTables[i] & PAGE_INHERITED)
					continue;

				// Ok, OUR user page-table, free everything in it
				PageTable_t *Pt = (PageTable_t*)Pd->vTables[i];

				// Iterate pages in table
				for (j = 0; j < PAGES_PER_TABLE; j++) {
					if (Pt->Pages[j] & PAGE_VIRTUAL)
						continue;

					// If it has a mapping - free it
					if ((Pt->Pages[j] & PAGE_MASK) != 0) {
						if (MmPhysicalFreeBlock(Pt->Pages[j] & PAGE_MASK) != OsSuccess) {
                            ERROR("Tried to free page %i (0x%x) , but was not allocated", j, Pt->Pages[j]);
                        }
					}
				}

				// Free the page-table
				kfree(Pt);
			}
		}
		kfree(AddressSpace);
	}
	else {
		SpinlockRelease(&AddressSpace->Lock);
	}
	return OsSuccess;
}

/* AddressSpaceSwitch
 * Switches the current address space out with the the address space provided 
 * for the current cpu */
OsStatus_t
AddressSpaceSwitch(
    _In_ AddressSpace_t *AddressSpace) {
	return MmVirtualSwitchPageDirectory(CpuGetCurrentId(),
		(PageDirectory_t*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], 
        (PhysicalAddress_t)AddressSpace->Data[ASPACE_DATA_CR3]);
}

/* AddressSpaceGetCurrent
 * Returns the current address space if there is no active threads or threading
 * is not setup it returns the kernel address space */
AddressSpace_t*
AddressSpaceGetCurrent(void)
{
	// Lookup current thread
	MCoreThread_t *CurrentThread = 
		ThreadingGetCurrentThread(CpuGetCurrentId());

	// if no threads are active return the kernel address space
	if (CurrentThread == NULL) {
		return &KernelAddressSpace;
	}
	else {
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
	Flags_t AllocFlags              = 0;
	int PageCount                   = 0;
	int i;

    // Assert that address space is not null
    assert(AddressSpace != NULL);

    // Calculate the number of pages of this allocation
    PageCount           = DIVUP(Size, PAGE_SIZE);

    // Determine the memory mappings initially
    if (Flags & ASPACE_FLAG_SUPPLIEDPHYSICAL) {
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
        if ((Flags & ASPACE_FLAG_CONTIGIOUS) || (Flags & ASPACE_FLAG_SUPPLIEDPHYSICAL)) {
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
			return OsError;
		}
	}
    return OsSuccess;
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

	// Iterate page-count and unmap
	for (i = 0; i < PageCount; i++) {
        if (MmVirtualGetMapping((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], (Address + (i * PAGE_SIZE))) != 0) {
            if (MmVirtualUnmap((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], (Address + (i * PAGE_SIZE))) != OsSuccess) {
                WARNING("Failed to unmap address 0x%x", (Address + (i * PAGE_SIZE)));
            }
        }
        else {
            WARNING("Freeing unmapped address 0x%x", (Address + (i * PAGE_SIZE)));
        }
	}
	return OsSuccess;
}

/* AddressSpaceGetMapping
 * Retrieves a physical mapping from an address space determined
 * by the virtual address given */
PhysicalAddress_t
AddressSpaceGetMapping(
    _In_ AddressSpace_t*    AddressSpace, 
    _In_ VirtualAddress_t   VirtualAddress) {
	return MmVirtualGetMapping((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], VirtualAddress);
}

/* AddressSpaceIsDirty
 * Checks if the given virtual address is dirty (has been written data to). 
 * Returns OsSuccess if the address is dirty. */
OsStatus_t
AddressSpaceIsDirty(
    _In_ AddressSpace_t*    AddressSpace,
    _In_ VirtualAddress_t   Address)
{
    Flags_t Flags = 0;
    if (MmVirtualGetFlags((void*)AddressSpace->Data[ASPACE_DATA_PDPOINTER], Address, &Flags) != OsSuccess) {
        return OsError;
    }
    if (Flags & PAGE_DIRTY) {
        return OsSuccess;
    }
    return OsError;
}

/* AddressSpaceGetPageSize
 * Retrieves the memory page-size used by the underlying architecture. */
size_t
AddressSpaceGetPageSize(void) {
    return PAGE_SIZE;
}
