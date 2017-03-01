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
 * MollenOS x86 Address Space Abstraction Layer
 */

/* Includes 
 * - System */
#include "../../arch.h"
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

/* Address Space Abstraction Layer
**********************************/

/* AddressSpaceInitKernel
 * Initializes the Kernel Address Space 
 * This only copies the data into a static global
 * storage, which means users should just pass something
 * temporary structure */
void AddressSpaceInitKernel(AddressSpace_t *Kernel)
{
	/* Copy data into ours */
	GlbKernelAddressSpace.Cr3 = Kernel->Cr3;
	GlbKernelAddressSpace.Flags = Kernel->Flags;
	GlbKernelAddressSpace.PageDirectory = Kernel->PageDirectory;

	/* Setup reference and lock */
	SpinlockReset(&GlbKernelAddressSpace.Lock);
	GlbKernelAddressSpace.References = 1;
}

/* AddressSpaceCreate
 * Initialize a new address space, depending on 
 * what user is requesting we might recycle a already
 * existing address space */
AddressSpace_t *AddressSpaceCreate(Flags_t Flags)
{
	/* Allocate Structure */
	AddressSpace_t *AddrSpace = NULL;
	Cpu_t CurrentCpu = ApicGetCpu();
	int Itr = 0;

	/* If we want to create a new kernel address
	 * space we instead want to re-use the current 
	 * If kernel is specified, ignore rest */
	if (Flags & ADDRESS_SPACE_KERNEL) {
		SpinlockAcquire(&GlbKernelAddressSpace.Lock);
		GlbKernelAddressSpace.References++;
		SpinlockRelease(&GlbKernelAddressSpace.Lock);
		AddrSpace = &GlbKernelAddressSpace;
	}
	else if (Flags == ADDRESS_SPACE_INHERIT) {
		/* Inheritance is a bit different, we re-use again
		 * but instead of reusing the kernel, we reuse the current */
		MCoreThread_t *Current = ThreadingGetCurrentThread(CurrentCpu);
		SpinlockAcquire(&Current->AddressSpace->Lock);
		Current->AddressSpace->References++;
		SpinlockRelease(&Current->AddressSpace->Lock);
		AddrSpace = Current->AddressSpace;
	}
	else if (Flags & (ADDRESS_SPACE_APPLICATION | ADDRESS_SPACE_DRIVER))
	{
		/* This is the only case where we should create a 
		 * new and seperate address space, user processes! */
		Addr_t PhysAddr = 0;
		PageDirectory_t *NewPd = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &PhysAddr);
		PageDirectory_t *CurrPd = (PageDirectory_t*)AddressSpaceGetCurrent()->PageDirectory;
		PageDirectory_t *KernPd = (PageDirectory_t*)GlbKernelAddressSpace.PageDirectory;

		/* Copy at max kernel directories up to MEMORY_SEGMENT_RING3_BASE */
		int KernelRegion = 0;
		int KernelRegionEnd = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_KERNEL_END);

		/* Lookup which table-region is the stack region */
		int StackRegion = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_STACK_END);
		int StackRegionEnd = PAGE_DIRECTORY_INDEX(MEMORY_SEGMENT_STACK_BASE);

		/* Allocate a new address space */
		AddrSpace = (AddressSpace_t*)kmalloc(sizeof(AddressSpace_t));

		/* Start out by resetting all */
		memset(NewPd, 0, sizeof(PageDirectory_t));

		/* Setup Lock */
		MutexConstruct(&NewPd->Lock);

		/* Create shared mappings */
		for (Itr = 0; Itr < TABLES_PER_PDIR; Itr++) {
			/* Sanity - Kernel Region */
			if (Itr >= KernelRegion && Itr < KernelRegionEnd) {
				NewPd->pTables[Itr] = KernPd->pTables[Itr];
				NewPd->vTables[Itr] = KernPd->vTables[Itr];
				continue;
			}

			/* Sanity - Stack Region */
			if (Itr >= StackRegion && Itr < StackRegionEnd) {
				continue;
			}

			/* Inherit? We must mark that table inherited to avoid
			 * it being freed again */
			if (Flags & ADDRESS_SPACE_INHERIT && CurrPd->pTables[Itr]) {
				NewPd->pTables[Itr] = CurrPd->pTables[Itr] | PAGE_INHERITED;
				NewPd->vTables[Itr] = CurrPd->vTables[Itr];
			}
		}

		/* Store the new data */
		AddrSpace->Flags = Flags;
		AddrSpace->Cr3 = PhysAddr;
		AddrSpace->PageDirectory = NewPd;

		/* Reset lock and reference count */
		SpinlockReset(&AddrSpace->Lock);
		AddrSpace->References = 1;
	}
	else
		LogFatal("VMEM", "Invalid flags parsed in AddressSpaceCreate 0x%x", Flags);

	/* Done */
	return AddrSpace;
}

/* AddressSpaceDestroy
 * Destroy and release all resources related
 * to an address space, only if there is no more
 * references */
void AddressSpaceDestroy(AddressSpace_t *AddrSpace)
{
	/* Acquire lock on the address space */
	SpinlockAcquire(&AddrSpace->Lock);

	/* Reduce reference count */
	AddrSpace->References--;

	/* Cleanup ? */
	if (AddrSpace->References == 0) {
		if (AddrSpace->Flags & (ADDRESS_SPACE_APPLICATION | ADDRESS_SPACE_DRIVER))
		{
			/* Vars */
			PageDirectory_t *KernPd = (PageDirectory_t*)GlbKernelAddressSpace.PageDirectory;
			PageDirectory_t *Pd = (PageDirectory_t*)AddrSpace->PageDirectory;
			int i, j;

			/* Iterate sections */
			for (i = 0; i < TABLES_PER_PDIR; i++)
			{
				/* Is there a page-table mapped here? */
				if (Pd->pTables[i] == 0)
					continue;

				/* Is it a kernel page-table? Ignore it */
				if (Pd->pTables[i] == KernPd->pTables[i])
					continue;

				/* Is this an inherited page-table?
				 * We don't free our parents stuff */
				if (Pd->pTables[i] & PAGE_INHERITED)
					continue;

				/* Ok, OUR user page-table, free everything in it */
				PageTable_t *Pt = (PageTable_t*)Pd->vTables[i];

				/* Iterate pages */
				for (j = 0; j < PAGES_PER_TABLE; j++)
				{
					/* Sanity */
					if (Pt->Pages[i] & PAGE_VIRTUAL)
						continue;

					/* Sanity */
					if (Pt->Pages[i] != 0)
						MmPhysicalFreeBlock(Pt->Pages[i] & PAGE_MASK);
				}

				/* Free page-table */
				kfree(Pt);
			}
		}

		/* Free structure */
		kfree(AddrSpace);
	}
	else {
		/* Release Lock */
		SpinlockRelease(&AddrSpace->Lock);
	}
}

/* AddressSpaceGetCurrent
 * Returns the current address space
 * if there is no active threads or threading
 * is not setup it returns the kernel address space */
AddressSpace_t *AddressSpaceGetCurrent(void)
{
	/* Get current thread */
	MCoreThread_t *CurrThread = 
		ThreadingGetCurrentThread(ApicGetCpu());

	/* Sanity */
	if (CurrThread == NULL)
		return &GlbKernelAddressSpace;
	else
		return CurrThread->AddressSpace;
}

/* AddressSpaceSwitch
 * Switches the current address space out with the
 * the address space provided for the current cpu */
void AddressSpaceSwitch(AddressSpace_t *AddrSpace)
{
	/* Get current cpu */
	Cpu_t CurrentCpu = ApicGetCpu();

	/* Redirect to our virtual memory manager */
	MmVirtualSwitchPageDirectory(CurrentCpu, 
		AddrSpace->PageDirectory, AddrSpace->Cr3);
}

/* AddressSpaceTranslate
 * Translates the given address to the correct virtual
 * address, this can be used to correct any special cases on
 * virtual addresses in the sub-layer */
Addr_t AddressSpaceTranslate(AddressSpace_t *AddrSpace, Addr_t VirtualAddress)
{
	/* Sanitize on the address, and the
	 * the type of addressing space */
	if (AddrSpace->Flags & ADDRESS_SPACE_KERNEL) {
		/* Never translate kernel address since the kernel
		 * segment spans the entire addressing space */
		return VirtualAddress;
	}
	else {
		return VirtualAddress;
	}
}

/* Map a virtual address into the Address Space
 * Returns the base physical address */
Addr_t AddressSpaceMap(AddressSpace_t *AddrSpace, VirtAddr_t Address, 
	size_t Size, Addr_t Mask, int Flags)
{
	/* Calculate num of pages */
	size_t PageCount = DIVUP(Size, PAGE_SIZE);
	Addr_t PhysicalBase = 0;
	Flags_t AllocFlags = 0;
	Addr_t RetAddress = 0;
	size_t Itr = 0;

	/* Add flags */
	if (Flags & ADDRESS_SPACE_FLAG_APPLICATION)
		AllocFlags |= PAGE_USER;
	if (Flags & ADDRESS_SPACE_FLAG_NOCACHE)
		AllocFlags |= PAGE_CACHE_DISABLE;
	if (Flags & ADDRESS_SPACE_FLAG_VIRTUAL)
		AllocFlags |= PAGE_VIRTUAL;
	if (Flags & ADDRESS_SPACE_FLAG_CONTIGIOUS) {
		RetAddress = PhysicalBase = MmPhysicalAllocateBlock(Mask, (int)PageCount);
	}

	/* Deep Call */
	for (Itr = 0; Itr < PageCount; Itr++) {
		Addr_t PhysBlock = 0;
		if (PhysicalBase != 0) {
			PhysBlock = PhysicalBase + (Itr * PAGE_SIZE);
		}
		else {
			PhysBlock = MmPhysicalAllocateBlock(Mask, 1);
		}

		/* Sanitize return, we return the physical mapping */
		if (RetAddress == 0) {
			RetAddress = PhysBlock;
		}

		/* Do the actual map */
		MmVirtualMap(AddrSpace->PageDirectory, PhysBlock,
			(Address + (Itr * PAGE_SIZE)), AllocFlags);
	}

	/* Done */
	return RetAddress;
}

/* Map a virtual address to a fixed physical page */
void AddressSpaceMapFixed(AddressSpace_t *AddrSpace,
	PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, size_t Size, int Flags)
{
	/* Calculate num of pages */
	size_t PageCount = DIVUP(Size, PAGE_SIZE);
	int AllocFlags = 0;
	size_t Itr = 0;

	/* Add flags */
	if (Flags & ADDRESS_SPACE_FLAG_APPLICATION)
		AllocFlags |= PAGE_USER;
	if (Flags & ADDRESS_SPACE_FLAG_NOCACHE)
		AllocFlags |= PAGE_CACHE_DISABLE;
	if (Flags & ADDRESS_SPACE_FLAG_VIRTUAL)
		AllocFlags |= PAGE_VIRTUAL;

	/* Deep Call */
	for (Itr = 0; Itr < PageCount; Itr++)
		MmVirtualMap(AddrSpace->PageDirectory, (PhysicalAddr + (Itr * PAGE_SIZE)),
						(VirtualAddr + (Itr * PAGE_SIZE)), (uint32_t)AllocFlags);
}

/* Unmaps a virtual page from an address space */
void AddressSpaceUnmap(AddressSpace_t *AddrSpace, VirtAddr_t Address, size_t Size)
{
	/* Calculate num of pages */
	size_t PageCount = DIVUP(Size, PAGE_SIZE);
	size_t Itr = 0;

	/* Deep Call */
	for (Itr = 0; Itr < PageCount; Itr++)
		MmVirtualUnmap(AddrSpace->PageDirectory, (Address + (Itr * PAGE_SIZE)));
}

/* AddressSpaceGetMap
 * Retrieves a physical mapping from an address space 
 * for x86 we can simply just redirect it to MmVirtual */
PhysAddr_t AddressSpaceGetMap(AddressSpace_t *AddrSpace, VirtAddr_t Address) {
	return MmVirtualGetMapping(AddrSpace->PageDirectory, Address);
}
