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

/* Includes 
 * - System */
#include <system/addresspace.h>
#include <threading.h>
#include <memory.h>
#include <heap.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Globals */
static AddressSpace_t GlbKernelAddressSpace;

/* AddressSpaceInitKernel
 * Initializes the Kernel Address Space 
 * This only copies the data into a static global
 * storage, which means users should just pass something
 * temporary structure */
OsStatus_t
AddressSpaceInitKernel(
	_In_ AddressSpace_t *Kernel)
{
	// Sanitize parameter
	assert(Kernel != NULL);

	// Copy data into our static storage
	GlbKernelAddressSpace.Cr3 = Kernel->Cr3;
	GlbKernelAddressSpace.Flags = Kernel->Flags;
	GlbKernelAddressSpace.PageDirectory = Kernel->PageDirectory;

	// Setup reference and lock
	SpinlockReset(&GlbKernelAddressSpace.Lock);
	GlbKernelAddressSpace.References = 1;

	// No errors
	return OsNoError;
}

/* AddressSpaceCreate
 * Initialize a new address space, depending on 
 * what user is requesting we might recycle a already
 * existing address space */
AddressSpace_t*
AddressSpaceCreate(
	_In_ Flags_t Flags)
{
	// Variables
	AddressSpace_t *AddressSpace = NULL;
	UUId_t CurrentCpu = ApicGetCpu();
	int Itr = 0;

	// If we want to create a new kernel address
	// space we instead want to re-use the current 
	// If kernel is specified, ignore rest 
	if (Flags & AS_TYPE_KERNEL) {
		SpinlockAcquire(&GlbKernelAddressSpace.Lock);
		GlbKernelAddressSpace.References++;
		SpinlockRelease(&GlbKernelAddressSpace.Lock);
		AddressSpace = &GlbKernelAddressSpace;
	}
	else if (Flags == AS_TYPE_INHERIT) {
		// Inheritance is a bit different, we re-use again
		// but instead of reusing the kernel, we reuse the current
		MCoreThread_t *Current = ThreadingGetCurrentThread(CurrentCpu);
		SpinlockAcquire(&Current->AddressSpace->Lock);
		Current->AddressSpace->References++;
		SpinlockRelease(&Current->AddressSpace->Lock);
		AddressSpace = Current->AddressSpace;
	}
	else if (Flags & (AS_TYPE_APPLICATION | AS_TYPE_DRIVER))
	{
		// This is the only case where we should create a 
		// new and seperate address space, user processes!
		Addr_t PhysAddr = 0;
		PageDirectory_t *NewPd = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &PhysAddr);
		PageDirectory_t *CurrPd = (PageDirectory_t*)AddressSpaceGetCurrent()->PageDirectory;
		PageDirectory_t *KernPd = (PageDirectory_t*)GlbKernelAddressSpace.PageDirectory;

		// Copy at max kernel directories up to MEMORY_SEGMENT_RING3_BASE
		int KernelRegion = 0;
		int KernelRegionEnd = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_KERNEL_END);

		// Lookup which table-region is the stack region
		int StackRegion = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_STACK_END);
		int StackRegionEnd = PAGE_DIRECTORY_INDEX(MEMORY_SEGMENT_STACK_BASE);

		// Allocate a new address space
		AddressSpace = (AddressSpace_t*)kmalloc(sizeof(AddressSpace_t));
		memset(NewPd, 0, sizeof(PageDirectory_t));

		// Initialize members
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
			if (Itr >= StackRegion && Itr < StackRegionEnd) {
				continue;
			}

			// Inherit? We must mark that table inherited to avoid
			// it being freed again
			if (Flags & AS_TYPE_INHERIT && CurrPd->pTables[Itr]) {
				NewPd->pTables[Itr] = CurrPd->pTables[Itr] | PAGE_INHERITED;
				NewPd->vTables[Itr] = CurrPd->vTables[Itr];
			}
		}

		// Store new configuration into AS
		AddressSpace->Flags = Flags;
		AddressSpace->Cr3 = PhysAddr;
		AddressSpace->PageDirectory = NewPd;

		// Reset lock and ref count
		SpinlockReset(&AddressSpace->Lock);
		AddressSpace->References = 1;
	}
	else {
		LogFatal("VMEM", "Invalid flags parsed in AddressSpaceCreate 0x%x", Flags);
	}

	/* Done */
	return AddressSpace;
}

/* AddressSpaceDestroy
 * Destroy and release all resources related
 * to an address space, only if there is no more
 * references */
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
		if (AddressSpace->Flags & (AS_TYPE_APPLICATION | AS_TYPE_DRIVER)) {
			PageDirectory_t *KernPd = (PageDirectory_t*)GlbKernelAddressSpace.PageDirectory;
			PageDirectory_t *Pd = (PageDirectory_t*)AddressSpace->PageDirectory;
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
					if (Pt->Pages[i] & PAGE_VIRTUAL)
						continue;

					// If it has a mapping - free it
					if (Pt->Pages[i] != 0) {
						MmPhysicalFreeBlock(Pt->Pages[i] & PAGE_MASK);
					}
				}

				// Free the page-table
				kfree(Pt);
			}
		}

		/* Free structure */
		kfree(AddressSpace);
	}
	else {
		SpinlockRelease(&AddressSpace->Lock);
	}

	// No errors
	return OsNoError;
}

/* AddressSpaceGetCurrent
 * Returns the current address space
 * if there is no active threads or threading
 * is not setup it returns the kernel address space */
AddressSpace_t*
AddressSpaceGetCurrent(void)
{
	// Lookup current thread
	MCoreThread_t *CurrThread = 
		ThreadingGetCurrentThread(ApicGetCpu());

	// if no threads are active return
	// the kernel address space
	if (CurrThread == NULL) {
		return &GlbKernelAddressSpace;
	}
	else {
		return CurrThread->AddressSpace;
	}
}

/* AddressSpaceSwitch
 * Switches the current address space out with the
 * the address space provided for the current cpu */
OsStatus_t
AddressSpaceSwitch(
	_In_ AddressSpace_t *AddressSpace)
{
	// Redirect to our virtual memory manager
	return MmVirtualSwitchPageDirectory(ApicGetCpu(),
		AddressSpace->PageDirectory, AddressSpace->Cr3);
}

/* AddressSpaceTranslate
 * Translates the given address to the correct virtual
 * address, this can be used to correct any special cases on
 * virtual addresses in the sub-layer */
VirtAddr_t
AddressSpaceTranslate(
	_In_ AddressSpace_t *AddressSpace,
	_In_ VirtAddr_t Address)
{
	// Sanitize on the address, and the
	// the type of addressing space 
	if (AddressSpace->Flags & AS_TYPE_KERNEL) {
		return Address;
	}
	else {
		return Address;
	}
}

/* AddressSpaceMap
 * Maps the given virtual address into the given address space
 * automatically allocates physical pages based on the passed Flags
 * It returns the start address of the allocated physical region */
PhysAddr_t
AddressSpaceMap(
	_In_ AddressSpace_t *AddressSpace,
	_In_ VirtAddr_t Address, 
	_In_ size_t Size,
	_In_ Addr_t Mask, 
	_In_ Flags_t Flags)
{
	// Variables
	size_t PageCount = DIVUP(Size, PAGE_SIZE);
	PhysAddr_t PhysicalBase = 0;
	Flags_t AllocFlags = 0;
	Addr_t RetAddress = 0;
	size_t Itr = 0;

	// Parse and convert flags
	if (Flags & AS_FLAG_APPLICATION) {
		AllocFlags |= PAGE_USER;
	}
	if (Flags & AS_FLAG_NOCACHE) {
		AllocFlags |= PAGE_CACHE_DISABLE;
	}
	if (Flags & AS_FLAG_VIRTUAL) {
		AllocFlags |= PAGE_VIRTUAL;
	}
	if (Flags & AS_FLAG_CONTIGIOUS) {
		RetAddress = PhysicalBase = MmPhysicalAllocateBlock(Mask, (int)PageCount);
	}

	// Iterate the number of pages to map 
	for (Itr = 0; Itr < PageCount; Itr++) {
		Addr_t PhysBlock = 0;
		if (PhysicalBase != 0) {
			PhysBlock = PhysicalBase + (Itr * PAGE_SIZE);
		}
		else {
			PhysBlock = MmPhysicalAllocateBlock(Mask, 1);
		}

		// Only return the base physical page
		if (RetAddress == 0) {
			RetAddress = PhysBlock;
		}

		// Redirect call to our virtual page manager
		MmVirtualMap(AddressSpace->PageDirectory, PhysBlock,
			(Address + (Itr * PAGE_SIZE)), AllocFlags);
	}

	/* Done */
	return RetAddress;
}

/* AddressSpaceMapFixed
 * Maps the given virtual address into the given address space
 * uses the given physical pages instead of automatic allocation
 * It returns the start address of the allocated physical region */
OsStatus_t
AddressSpaceMapFixed(
	_In_ AddressSpace_t *AddressSpace,
	_In_ PhysAddr_t pAddress, 
	_In_ VirtAddr_t vAddress, 
	_In_ size_t Size, 
	_In_ Flags_t Flags)
{
	// Variables
	size_t PageCount = DIVUP(Size, PAGE_SIZE);
	int AllocFlags = 0;
	size_t Itr = 0;

	// Parse and convert flags
	// MapFixed does not support CONTIGIOUS
	if (Flags & AS_FLAG_APPLICATION) {
		AllocFlags |= PAGE_USER;
	}
	if (Flags & AS_FLAG_NOCACHE) {
		AllocFlags |= PAGE_CACHE_DISABLE;
	}
	if (Flags & AS_FLAG_VIRTUAL) {
		AllocFlags |= PAGE_VIRTUAL;
	}

	// Now map it in by redirecting to virtual memory manager
	for (Itr = 0; Itr < PageCount; Itr++) {
		MmVirtualMap(AddressSpace->PageDirectory, (pAddress + (Itr * PAGE_SIZE)),
			(vAddress + (Itr * PAGE_SIZE)), (uint32_t)AllocFlags);
	}

	// No errors
	return OsNoError;
}

/* AddressSpaceUnmap
 * Unmaps a virtual memory region from an address space */
OsStatus_t
AddressSpaceUnmap(
	_In_ AddressSpace_t *AddressSpace, 
	_In_ VirtAddr_t Address, 
	_In_ size_t Size)
{
	// Variables
	size_t PageCount = DIVUP(Size, PAGE_SIZE);
	size_t Itr = 0;

	// Iterate page-count and unmap
	for (Itr = 0; Itr < PageCount; Itr++) {
		MmVirtualUnmap(AddressSpace->PageDirectory, (Address + (Itr * PAGE_SIZE)));
	}
	
	// Done - no errors
	return OsNoError;
}

/* AddressSpaceGetMap
 * Retrieves a physical mapping from an address space determined
 * by the virtual address given */
PhysAddr_t
AddressSpaceGetMap(
	_In_ AddressSpace_t *AddressSpace, 
	_In_ VirtAddr_t Address) {
	return MmVirtualGetMapping(AddressSpace->PageDirectory, Address);
}
