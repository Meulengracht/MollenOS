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
* MollenOS x86-32 Virtual Memory Manager
*/

#include <Arch.h>
#include <Heap.h>
#include <Video.h>
#include <Memory.h>
#include <Mutex.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Globals */
PageDirectory_t *KernelPageDirectory = NULL;
PageDirectory_t *CurrentPageDirectories[64];
volatile Addr_t GblReservedPtr = 0;
Mutex_t VmMutex;

/* Externs */
extern volatile uint32_t num_cpus;
extern Graphics_t gfx_info;
extern SysMemMapping_t SysMappings[32];
extern void memory_set_paging(int enable);
extern void memory_load_cr3(Addr_t pda);
extern void memory_reload_cr3(void);
extern void memory_invalidate_addr(Addr_t pda);

/* Create a page-table */
PageTable_t *MmVirtualCreatePageTable(void)
{
	/* Allocate a page table */
	PhysAddr_t addr = MmPhysicalAllocateBlock();
	PageTable_t *ptable = (PageTable_t*)addr;

	/* Sanity */
	assert((PhysAddr_t)ptable > 0);

	/* Zero it */
	memset((void*)ptable, 0, sizeof(PageTable_t));

	/* Done */
	return ptable;
}

/* Identity maps an address range */
void MmVirtualFillPageTable(PageTable_t *pTable, PhysAddr_t PhysStart, VirtAddr_t VirtStart)
{
	/* Iterators */
	Addr_t phys, virt;
	uint32_t i;

	/* Identity Map */
	for (i = PAGE_TABLE_INDEX(VirtStart), phys = PhysStart, virt = VirtStart;
		i < 1024;
		i++, phys += PAGE_SIZE, virt += PAGE_SIZE)
	{
		/* Create Entry */
		uint32_t page = phys | PAGE_PRESENT | PAGE_WRITE;

		/* Set it at correct offset */
		pTable->Pages[PAGE_TABLE_INDEX(virt)] = page;
	}
}

/* Map physical range to virtual range */
void MmVirtualIdentityMapMemoryRange(PageDirectory_t* PageDirectory,
	PhysAddr_t PhysStart, VirtAddr_t VirtStart, Addr_t Size, uint32_t Fill, uint32_t Flags)
{
	uint32_t i, k;

	for (i = PAGE_DIRECTORY_INDEX(VirtStart), k = 0;
		i < (PAGE_DIRECTORY_INDEX(VirtStart + Size - 1) + 1);
		i++, k++)
	{
		/* Initialize new page table */
		PageTable_t *ptable = MmVirtualCreatePageTable();

		/* Get addresses that match page table */
		uint32_t current_phys = PhysStart + (k * TABLE_SPACE_SIZE);
		uint32_t current_virt = VirtStart + (k * TABLE_SPACE_SIZE);

		/* Fill it */
		if (Fill != 0)
			MmVirtualFillPageTable(ptable, current_phys, current_virt);

		/* Install Table */
		PageDirectory->pTables[i] = (PhysAddr_t)ptable | PAGE_PRESENT | PAGE_WRITE | Flags;
		PageDirectory->vTables[i] = (Addr_t)ptable;
	}
}

/* Updates the current CPU current directory */
void MmVirtualSwitchPageDirectory(Cpu_t cpu, PageDirectory_t* PageDirectory, PhysAddr_t Pdb)
{
	/* Sanity */
	assert(PageDirectory != NULL);

	/* Update Current */
	CurrentPageDirectories[cpu] = PageDirectory;

	/* Switch */
	memory_load_cr3(Pdb);

	/* Done */
	return;
}

/* Returns current memory directory */
PageDirectory_t *MmVirtualGetCurrentDirectory(Cpu_t cpu)
{
	return CurrentPageDirectories[cpu];
}

/* Install paging for AP Cores */
void MmVirtualInstallPaging(Cpu_t cpu)
{
	/* Enable paging */
	MmVirtualSwitchPageDirectory(cpu, KernelPageDirectory, (Addr_t)KernelPageDirectory);
	memory_set_paging(1);
}

/* Maps a virtual memory address to a physical
 * memory address in a given page-directory 
 * If page-directory is NULL, current directory
 * is used */
void MmVirtualMap(void *PageDirectory, PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, uint32_t Flags)
{
	PageDirectory_t *pdir = (PageDirectory_t*)PageDirectory;
	PageTable_t *ptable = NULL;
	
	/* Determine page directory */
	if (pdir == NULL)
	{
		/* Get CPU */
		pdir = (PageDirectory_t*)CurrentPageDirectories[ApicGetCpu()];
	}

	/* Sanity */
	assert(pdir != NULL);

	/* Get mutex */
	MutexLock(&pdir->pMutex);

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* No... Create it */
		Addr_t phys_table = 0;
		PageTable_t *ntable = NULL;

		/* Allocate new table */
		ntable = (PageTable_t*)kmalloc_ap(PAGE_SIZE, &phys_table);

		/* Sanity */
		assert((Addr_t)ntable > 0);

		/* Zero it */
		memset((void*)ntable, 0, sizeof(PageTable_t));

		/* Install it */
		pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = phys_table | PAGE_PRESENT | PAGE_WRITE | Flags;
		pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = (Addr_t)ntable;

		/* Reload CR3 */
		if (PageDirectory == NULL)
			memory_reload_cr3();
	}

	/* Get it */
	ptable = (PageTable_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Now, lets map page! */
	if (ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] != 0)
	{
		printf("Trying to remap virtual 0x%x to physical 0x%x (original mapping 0x%x)\n",
			VirtualAddr, PhysicalAddr, ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)]);
		for (;;);
		return;
	}

	/* Map it */
	ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] = (PhysicalAddr & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | Flags;

	/* Release mutex */
	MutexUnlock(&pdir->pMutex);

	/* Invalidate Address */
	if (PageDirectory == NULL)
		memory_invalidate_addr(VirtualAddr);
}

/* Unmaps a virtual memory address and frees the physical
* memory address in a given page-directory
* If page-directory is NULL, current directory
* is used */
void MmVirtualUnmap(void *PageDirectory, VirtAddr_t VirtualAddr)
{
	PageDirectory_t *pdir = (PageDirectory_t*)PageDirectory;
	PageTable_t *ptable = NULL;
	PhysAddr_t phys = 0;

	/* Determine page directory */
	if (pdir == NULL)
	{
		/* Get CPU */
		pdir = (PageDirectory_t*)CurrentPageDirectories[ApicGetCpu()];
	}

	/* Sanity */
	assert(pdir != NULL);

	/* Get mutex */
	MutexLock(&pdir->pMutex);

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* No... What the fuck? */
		
		/* Release mutex */
		MutexUnlock(&pdir->pMutex);

		/* Return */
		return;
	}

	/* Get it */
	ptable = (PageTable_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Sanity */
	if (ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] == 0)
	{
		/* Release mutex */
		MutexUnlock(&pdir->pMutex);

		/* Return */
		return;
	}

	/* Do it */
	phys = ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)];
	ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] = 0;

	/* Release memory */
	MmPhysicalFreeBlock(phys);

	/* Release mutex */
	MutexUnlock(&pdir->pMutex);

	/* Invalidate Address */
	if (PageDirectory == NULL)
		memory_invalidate_addr(VirtualAddr);
}

/* Gets a physical memory address from a virtual
* memory address in a given page-directory
* If page-directory is NULL, current directory
* is used */
PhysAddr_t MmVirtualGetMapping(void *PageDirectory, VirtAddr_t VirtualAddr)
{
	PageDirectory_t *pdir = (PageDirectory_t*)PageDirectory;
	PageTable_t *ptable = NULL;
	PhysAddr_t phys = 0;

	/* Determine page directory */
	if (pdir == NULL)
	{
		/* Get CPU */
		pdir = (PageDirectory_t*)CurrentPageDirectories[ApicGetCpu()];
	}

	/* Sanity */
	assert(pdir != NULL);

	/* Get mutex */
	MutexLock(&pdir->pMutex);

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* No... */

		/* Release mutex */
		MutexUnlock(&pdir->pMutex);

		/* Return */
		return phys;
	}

	/* Get it */
	ptable = (PageTable_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Sanity */
	assert(ptable != NULL);

	/* Return mapping */
	phys = ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] & PAGE_MASK;

	/* Release mutex */
	MutexUnlock(&pdir->pMutex);

	/* Sanity */
	if (phys == 0)
		return phys;

	/* Done - Return with offset */
	return (phys + (VirtualAddr & ATTRIBUTE_MASK));
}

/* Maps a virtual memory address to a physical
* memory address in a given page-directory
* If page-directory is NULL, current directory
* is used */
void MmVirtualInitialMap(PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr)
{
	PageDirectory_t *pdir = KernelPageDirectory;
	PageTable_t *ptable = NULL;

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* No... Create it */
		PageTable_t *ntable = MmVirtualCreatePageTable();

		/* Zero it */
		memset((void*)ntable, 0, sizeof(PageTable_t));

		/* Install it */
		pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = (PhysAddr_t)ntable | PAGE_PRESENT | PAGE_WRITE;
		pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = (PhysAddr_t)ntable;
	}

	/* Get it */
	ptable = (PageTable_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Now, lets map page! */
	assert(ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] == 0
		&& "Dont remap pages without freeing :(");

	/* Map it */
	ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] = (PhysicalAddr & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE;
}

/* Map system memory */
VirtAddr_t *MmVirtualMapSysMemory(PhysAddr_t PhysicalAddr, int Pages)
{
	int i;
	Cpu_t cpu;
	VirtAddr_t ret = 0;

	/* Get cpu */
	cpu = ApicGetCpu();

	/* Acquire Lock */
	MutexLock(&VmMutex);

	/* This is the addr that we return */
	ret = GblReservedPtr;

	/* Map it */
	for (i = 0; i < Pages; i++)
	{
		/* Call Map */
		if (!MmVirtualGetMapping(MmVirtualGetCurrentDirectory(cpu), GblReservedPtr))
			MmVirtualMap(MmVirtualGetCurrentDirectory(cpu), PhysicalAddr + (i * PAGE_SIZE), GblReservedPtr, 0);

		/* Increase */
		GblReservedPtr += PAGE_SIZE;
	}

	/* Release */
	MutexUnlock(&VmMutex);

	/* Return converted address with correct offset */
	return (VirtAddr_t*)(ret + (PhysicalAddr & ATTRIBUTE_MASK));
}

/* Creates a page directory and loads it */
void MmVirtualInit(void)
{
	/* Variables we need */
	uint32_t i;
	PageTable_t *itable;

	/* Allocate space */
	num_cpus = 0;
	GblReservedPtr = MEMORY_LOCATION_RESERVED;

	/* We need 3 pages for the page directory */
	KernelPageDirectory = (PageDirectory_t*)MmPhysicalAllocateBlock();
	MmPhysicalAllocateBlock(); MmPhysicalAllocateBlock();

	/* Allocate initial */
	itable = MmVirtualCreatePageTable();

	/* Identity map only first 4 mB (THIS IS KERNEL ONLY) */
	MmVirtualFillPageTable(itable, 0x1000, 0x1000);

	/* Clear out page_directory */
	memset((void*)KernelPageDirectory, 0, sizeof(PageDirectory_t));

	/* Install it */
	KernelPageDirectory->pTables[0] = (PhysAddr_t)itable | PAGE_PRESENT | PAGE_WRITE;
	KernelPageDirectory->vTables[0] = (Addr_t)itable;
	
	/* Init mutexes */
	MutexConstruct(&KernelPageDirectory->pMutex);
	MutexConstruct(&VmMutex);

	/* Map Memory Regions */

	/* HEAP */
	printf("      > Mapping heap region to 0x%x\n", MEMORY_LOCATION_HEAP);
	MmVirtualIdentityMapMemoryRange(KernelPageDirectory, 0, MEMORY_LOCATION_HEAP,
		(MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP), 0, 0);

	/* SHARED MEMORY */
	printf("      > Mapping shared memory region to 0x%x\n", MEMORY_LOCATION_SHM);
	MmVirtualIdentityMapMemoryRange(KernelPageDirectory, 0, MEMORY_LOCATION_SHM,
		(MEMORY_LOCATION_SHM_END - MEMORY_LOCATION_SHM), 0, PAGE_USER);

	/* VIDEO MEMORY (WITH FILL) */
	printf("      > Mapping video memory to 0x%x\n", MEMORY_LOCATION_VIDEO);
	MmVirtualIdentityMapMemoryRange(KernelPageDirectory, gfx_info.VideoAddr,
		MEMORY_LOCATION_VIDEO, (gfx_info.BytesPerScanLine * gfx_info.ResY), 1, PAGE_USER);

	/* Now, tricky, map reserved memory regions */

	/* Step 1. Install a pagetable at MEMORY_LOCATION_RESERVED */
	printf("      > Mapping reserved memory to 0x%x\n", MEMORY_LOCATION_RESERVED);

	/* Step 2. Map */
	for (i = 0; i < 32; i++)
	{
		if (SysMappings[i].Length != 0 && SysMappings[i].Type != 1)
		{
			/* Get page count */
			size_t page_length = SysMappings[i].Length / PAGE_SIZE;
			uint32_t k;

			/* Round up */
			if (SysMappings[i].Length % PAGE_SIZE)
				page_length++;

			/* Update entry */
			SysMappings[i].VirtualAddrStart = GblReservedPtr;

			/* Map it */
			for (k = 0; k < page_length; k++)
			{ 
				/* Call Map */
				MmVirtualInitialMap(((SysMappings[i].PhysicalAddrStart & PAGE_MASK) + (k * PAGE_SIZE)), GblReservedPtr);

				/* Increase */
				GblReservedPtr += PAGE_SIZE;
			}
		}
	}

	/* Modify Video Address */
	gfx_info.VideoAddr = MEMORY_LOCATION_VIDEO;

	/* Enable paging */
	MmVirtualSwitchPageDirectory(0, KernelPageDirectory, (Addr_t)KernelPageDirectory);
	memory_set_paging(1);
}