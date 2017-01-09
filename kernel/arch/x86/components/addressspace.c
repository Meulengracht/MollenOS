/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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

/* Includes */
#include "../../arch.h"
#include <Threading.h>
#include <Devices/Video.h>
#include <Heap.h>
#include <Video.h>
#include <Memory.h>
#include <Log.h>

/* C-Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Globals */
static AddressSpace_t GlbKernelAddressSpace;

/* Address Space Abstraction Layer
**********************************/

/* Initializes the Kernel Address Space 
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

/* Initialize a new address space, depending on 
 * what user is requesting we might recycle a already
 * existing address space */
AddressSpace_t *AddressSpaceCreate(int Flags)
{
	/* Allocate Structure */
	AddressSpace_t *AddrSpace = NULL;
	Cpu_t CurrentCpu = ApicGetCpu();
	int Itr = 0;

	/* Depends on what caller wants */
	if (Flags & ADDRESS_SPACE_KERNEL) 
	{
		/* Lock and increase reference count on kernel
		 * address space, we reuse */
		SpinlockAcquire(&GlbKernelAddressSpace.Lock);

		/* Increase Count */
		GlbKernelAddressSpace.References++;

		/* Release lock */
		SpinlockRelease(&GlbKernelAddressSpace.Lock);

		/* Done! */
		AddrSpace = &GlbKernelAddressSpace;
	}
	else if (Flags == ADDRESS_SPACE_INHERIT)
	{
		/* Get current address space */
		MCoreThread_t *Current = ThreadingGetCurrentThread(CurrentCpu);

		/* Lock and increase reference count on kernel
		* address space, we reuse */
		SpinlockAcquire(&Current->AddressSpace->Lock);

		/* Increase Count */
		Current->AddressSpace->References++;

		/* Release lock */
		SpinlockRelease(&Current->AddressSpace->Lock);

		/* Done! */
		AddrSpace = Current->AddressSpace;
	}
	else if (Flags & ADDRESS_SPACE_USER)
	{
		/* This is the only case where we should create a 
		 * new and seperate address space, user processes! */
		Addr_t PhysAddr = 0;
		PageDirectory_t *NewPd = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &PhysAddr);
		PageDirectory_t *CurrPd = (PageDirectory_t*)AddressSpaceGetCurrent()->PageDirectory;
		PageDirectory_t *KernPd = (PageDirectory_t*)GlbKernelAddressSpace.PageDirectory;
		int MaxCopyKernel = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_USER_ARGS);
		int MaxCopyReadOnly = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_VIDEO);

		/* Allocate a new address space */
		AddrSpace = (AddressSpace_t*)kmalloc(sizeof(AddressSpace_t));

		/* Start out by resetting all */
		memset(NewPd, 0, sizeof(PageDirectory_t));

		/* Setup Lock */
		MutexConstruct(&NewPd->Lock);

		/* Create shared mappings */
		for (Itr = 0; Itr < TABLES_PER_PDIR; Itr++)
		{
			/* Sanity - Kernel Mapping 
			 * Only copy kernel mappings BELOW user-space start */
			if (KernPd->pTables[Itr] && Itr < MaxCopyKernel) {
				if (Itr < MaxCopyReadOnly) {
					NewPd->pTables[Itr] = (KernPd->pTables[Itr] & PAGE_MASK)
						| PAGE_PRESENT | PAGE_INHERITED ;
				}
				else {
					NewPd->pTables[Itr] = KernPd->pTables[Itr];
				}

				NewPd->vTables[Itr] = KernPd->vTables[Itr];
				continue;
			}

			/* Inherit? (Yet, never inherit last pagedir, thats where stack is) */
			if (Flags & ADDRESS_SPACE_INHERIT
				&& Itr != (TABLES_PER_PDIR - 1)
				&& CurrPd->pTables[Itr]) {
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

/* Destroy and release all resources related
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
		/* Sanity */
		if (AddrSpace->Flags & ADDRESS_SPACE_FLAG_USER)
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

/* Returns the current address space
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

/* Switch to given address space */
void AddressSpaceSwitch(AddressSpace_t *AddrSpace)
{
	/* Get current cpu */
	Cpu_t CurrentCpu = ApicGetCpu();

	/* Deep Call */
	MmVirtualSwitchPageDirectory(CurrentCpu, 
		AddrSpace->PageDirectory, AddrSpace->Cr3);
}

/* This function removes kernel mappings from 
 * an address space, thus protecting the kernel
 * from access! */
void AddressSpaceReleaseKernel(AddressSpace_t *AddrSpace)
{
	/* Vars */
	PageDirectory_t *Pd = (PageDirectory_t*)AddrSpace->PageDirectory;
	int Itr = 0;

	/* Now unmap */
	for (Itr = 0; Itr < PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_USER_ARGS) - 1; Itr++) {
		Pd->pTables[Itr] = 0;
		Pd->vTables[Itr] = 0;
	}
}

/* Map a virtual address into the Address Space
 * Returns the base physical address */
Addr_t AddressSpaceMap(AddressSpace_t *AddrSpace, VirtAddr_t Address, 
	size_t Size, Addr_t Mask, int Flags)
{
	/* Calculate num of pages */
	size_t PageCount = DIVUP(Size, PAGE_SIZE);
	Flags_t AllocFlags = 0;
	Addr_t RetAddr = 0;
	size_t Itr = 0;

	/* Add flags */
	if (Flags & ADDRESS_SPACE_FLAG_USER)
		AllocFlags |= PAGE_USER;
	if (Flags & ADDRESS_SPACE_FLAG_NOCACHE)
		AllocFlags |= PAGE_CACHE_DISABLE;
	if (Flags & ADDRESS_SPACE_FLAG_VIRTUAL)
		AllocFlags |= PAGE_VIRTUAL;

	/* Deep Call */
	for (Itr = 0; Itr < PageCount; Itr++)
	{
		/* Alloc physical page */
		Addr_t PhysBlock = MmPhysicalAllocateBlock(Mask, 1);

		/* Sanity */
		if (RetAddr == 0)
			RetAddr = PhysBlock;

		/* Do the actual map */
		MmVirtualMap(AddrSpace->PageDirectory, PhysBlock,
			(Address + (Itr * PAGE_SIZE)), AllocFlags);
	}

	/* Done */
	return RetAddr;
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
	if (Flags & ADDRESS_SPACE_FLAG_USER)
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

/* Retrieves a physical mapping from an address space 
 * for x86 we can simply just redirect it to MmVirtual */
PhysAddr_t AddressSpaceGetMap(AddressSpace_t *AddrSpace, VirtAddr_t Address) {
	return MmVirtualGetMapping(AddrSpace->PageDirectory, Address);
}
