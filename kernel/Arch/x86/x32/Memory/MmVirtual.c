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
* Todo - Re-evalution of source
*/

#include <Threading.h>
#include <Devices/Video.h>
#include <Heap.h>
#include <Video.h>
#include <Memory.h>
#include <Log.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Globals */
PageDirectory_t *KernelPageDirectory = NULL;
PageDirectory_t *CurrentPageDirectories[64];
volatile Addr_t GblReservedPtr = 0;

/* Lock */
Spinlock_t VmLock;

/* Externs */
extern MCoreVideoDevice_t GlbBootVideo;
extern SysMemMapping_t SysMappings[32];
extern void memory_set_paging(int enable);
extern void memory_load_cr3(Addr_t pda);
extern void memory_reload_cr3(void);
extern void memory_invalidate_addr(Addr_t pda);
extern uint32_t memory_get_cr3(void);

/* Extern these for their addresses */
extern void exception_common(void);
extern void irq_handler255(void);

/* Create a page-table */
PageTable_t *MmVirtualCreatePageTable(void)
{
	/* Allocate a page table */
	PhysAddr_t pAddr = MmPhysicalAllocateBlock(MEMORY_INIT_MASK, 1);
	PageTable_t *pTable = (PageTable_t*)pAddr;

	/* Sanity */
	assert((PhysAddr_t)pTable > 0);

	/* Zero it */
	memset((void*)pTable, 0, sizeof(PageTable_t));

	/* Done */
	return pTable;
}

/* Identity maps an address range */
void MmVirtualFillPageTable(PageTable_t *pTable, PhysAddr_t PhysStart, VirtAddr_t VirtStart, uint32_t Flags)
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
		uint32_t pAttribs = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP | Flags;

		/* Set it at correct offset */
		pTable->Pages[PAGE_TABLE_INDEX(virt)] = pAttribs;
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
		PageTable_t *pTable = MmVirtualCreatePageTable();

		/* Get addresses that match page table */
		uint32_t CurrPhys = PhysStart + (k * TABLE_SPACE_SIZE);
		uint32_t CurrVirt = VirtStart + (k * TABLE_SPACE_SIZE);

		/* Fill it */
		if (Fill != 0)
			MmVirtualFillPageTable(pTable, CurrPhys, CurrVirt, Flags);

		/* Install Table */
		PageDirectory->pTables[i] = (PhysAddr_t)pTable | (PAGE_SYSTEM_MAP | PAGE_PRESENT | PAGE_WRITE | Flags);
		PageDirectory->vTables[i] = (Addr_t)pTable;
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
	/* Variables we need to map a new 
	 * entry in, in the page-directory */
	PageDirectory_t *Directory = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table = NULL;
	int IsCurrent = 0;

	/* Determine page directory 
	 * If we were given null, select for core */
	if (Directory == NULL) {
		Directory = CurrentPageDirectories[ApicGetCpu()];
	}

	/* Sanitizie if we are modifying the currently
	 * loaded page-directory */
	if (CurrentPageDirectories[ApicGetCpu()] == Directory) {
		IsCurrent = 1;
	}

	/* Sanitize again
	 * If its still null something is wrong */
	assert(Directory != NULL);

	/* Get lock on the page-directory 
	 * we don't want people to touch */
	MutexLock(&Directory->Lock);

	/* Does page table exist? 
	 * If the page-table is not even mapped in we need to 
	 * do that beforehand */
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* Variables for creation */
		Addr_t TablePhysical = 0;

		/* Allocate new table */
		Table = (PageTable_t*)kmalloc_ap(PAGE_SIZE, &TablePhysical);

		/* Sanitize the newly allocated
		 * table, two things must be true;
		 * no NULL and no page-not-align */
		assert(Table != NULL);

		/* Zero it */
		memset((void*)Table, 0, sizeof(PageTable_t));

		/* Install it into our directory, now if the address
		 * we are mapping is user-accessible, we should add flags */
		Directory->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = 
			TablePhysical | PAGE_PRESENT | PAGE_WRITE | Flags;
		Directory->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = 
			(Addr_t)Table;

		/* Reload CR3 directory to force 
		 * the MMIO to see our changes */
		if (IsCurrent) {
			memory_reload_cr3();
		}
	}
	else {
		/* Simply load it from the directory table */
		Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];
	}

	/* Sanitize the table before we use it 
	 * otherwise we might fuck up */
	assert(Table != NULL);

	/* Sanitize that the index isn't already
	 * mapped in, thats a fatality */
	if (Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)] != 0) {
		LogFatal("VMEM", "Trying to remap virtual 0x%x to physical 0x%x (original mapping 0x%x)",
			VirtualAddr, PhysicalAddr, Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)]);
		kernel_panic("This is not good");
	}

	/* Map it, make sure we mask the page address
	 * so we don't accidently set any flags */
	Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)] =
		(PhysicalAddr & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | Flags;

	/* Release lock! we are done! */
	MutexUnlock(&Directory->Lock);

	/* Last step is to invalidate the 
	 * the address in the MMIO */
	if (IsCurrent) {
		memory_invalidate_addr(VirtualAddr);
	}
}

/* Unmaps a virtual memory address and frees the physical
 * memory address in a given page-directory
 * If page-directory is NULL, current directory
 * is used */
void MmVirtualUnmap(void *PageDirectory, VirtAddr_t VirtualAddr)
{
	/* Variables needed for finding
	 * out page index */
	PageDirectory_t *Directory = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table = NULL;
	int IsCurrent = 0;

	/* Determine page directory 
	 * if pDir is null we get for current cpu */
	if (Directory == NULL) {
		Directory = CurrentPageDirectories[ApicGetCpu()];
	}

	/* Sanitizie if we are modifying the currently
	* loaded page-directory */
	if (CurrentPageDirectories[ApicGetCpu()] == Directory) {
		IsCurrent = 1;
	}

	/* Sanitize the page-directory
	 * If it's still NULL somethings wrong */
	assert(Directory != NULL);

	/* Acquire the mutex */
	MutexLock(&Directory->Lock);

	/* Does page table exist? 
	 * or is a system table, we can't unmap these! */
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT)
		|| (Directory->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_SYSTEM_MAP)) {
		goto Leave;
	}

	/* Acquire the proper page-table */
	Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Sanitize the page-index, if it's not mapped in
	 * then we are trying to unmap somethings that not even mapped */
	assert(Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)] != 0);

	/* System memory? Don't unmap, for gods sake */
	if (Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)] & PAGE_SYSTEM_MAP) {
		goto Leave;
	}
	else
	{
		/* Ok, step one is to extract the physical page
		 * of this index */
		PhysAddr_t Physical = Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)];

		/* Clear the mapping out */
		Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)] = 0;

		/* Release memory, but don't if it 
		 * is a virtual mapping, that means we should not free
		 * the physical page */
		if (!(Physical & PAGE_VIRTUAL)) {
			MmPhysicalFreeBlock(Physical & PAGE_MASK);
		}

		/* Last step is to validate the page-mapping
		 * now this should be an IPC to all cpu's */
		if (IsCurrent) {
			memory_invalidate_addr(VirtualAddr);
		}
	}

Leave:
	
	/* Release the mutex and allow 
	 * others to use the page-directory */
	MutexUnlock(&Directory->Lock);
}

/* Gets a physical memory address from a virtual
 * memory address in a given page-directory
 * If page-directory is NULL, current directory
 * is used */
PhysAddr_t MmVirtualGetMapping(void *PageDirectory, VirtAddr_t VirtualAddr)
{
	/* Variables needed for page-directory access */
	PageDirectory_t *Directory = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table = NULL;
	PhysAddr_t Mapping = 0;

	/* Determine page directory */
	if (Directory == NULL) {
		Directory = CurrentPageDirectories[ApicGetCpu()];
	}

	/* Sanitize the page-directory
	* If it's still NULL somethings wrong */
	assert(Directory != NULL);

	/* Acquire the mutex */
	MutexLock(&Directory->Lock);

	/* Is the table even present in the directory? 
	 * If not, then no mapping */
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT)) {
		goto NotMapped;
	}

	/* Fetch the page table from the 
	 * page-directory */
	Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Sanitize the page-table just in case */
	assert(Table != NULL);

	/* Sanitize the mapping before anything */
	if (!(Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)] & PAGE_PRESENT)) {
		goto NotMapped;
	}

	/* Retrieve mapping */
	Mapping = Table->Pages[PAGE_TABLE_INDEX(VirtualAddr)] & PAGE_MASK;

	/* Release mutex on page-directory
	 * we should not keep it longer than neccessary */
	MutexUnlock(&Directory->Lock);

	/* Done - Return with offset */
	return (Mapping + (VirtualAddr & ATTRIBUTE_MASK));

NotMapped:
	/* Release mutex on page-directory
	 * we should not keep it longer than neccessary */
	MutexUnlock(&Directory->Lock);

	/* Return 0, no mapping */
	return 0;
}

/* Maps a virtual memory address to a physical
* memory address in a given page-directory
* If page-directory is NULL, current directory
* is used */
void MmVirtualInitialMap(PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr)
{
	PageDirectory_t *pDir = KernelPageDirectory;
	PageTable_t *pTable = NULL;

	/* Does page table exist? */
	if (!(pDir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* No... Create it */
		PageTable_t *ntable = MmVirtualCreatePageTable();

		/* Zero it */
		memset((void*)ntable, 0, sizeof(PageTable_t));

		/* Install it */
		pDir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = (PhysAddr_t)ntable | PAGE_PRESENT | PAGE_WRITE;
		pDir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = (PhysAddr_t)ntable;
	}

	/* Get it */
	pTable = (PageTable_t*)pDir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Now, lets map page! */
	assert(pTable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] == 0
		&& "Dont remap pages without freeing :(");

	/* Map it */
	pTable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] = (PhysicalAddr & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE;
}

/* Map system memory */
VirtAddr_t *MmReserveMemory(int Pages)
{
	VirtAddr_t RetAddr = 0;

	/* Acquire Lock */
	SpinlockAcquire(&VmLock);

	/* This is the addr that we return */
	RetAddr = GblReservedPtr;

	/* Increase */
	GblReservedPtr += (PAGE_SIZE * Pages);

	/* Release */
	SpinlockRelease(&VmLock);

	/* Return address */
	return (VirtAddr_t*)RetAddr;
}

/* Creates a page directory and loads it */
void MmVirtualInit(void)
{
	/* Variables we need */
	AddressSpace_t KernelAddrSpace;
	PageTable_t *iTable;
	int i;

	/* Allocate space */
	GblReservedPtr = MEMORY_LOCATION_RESERVED;

	/* Info */
	LogInformation("VMEM", "Initializing");

	/* We need 3 pages for the page directory */
	KernelPageDirectory = (PageDirectory_t*)MmPhysicalAllocateBlock(MEMORY_INIT_MASK, 3);

	/* Allocate initial */
	iTable = MmVirtualCreatePageTable();

	/* Identity map only first 4 mB (THIS IS KERNEL ONLY) */
	MmVirtualFillPageTable(iTable, 0x1000, 0x1000, 0);

	/* Clear out page_directory */
	memset((void*)KernelPageDirectory, 0, sizeof(PageDirectory_t));

	/* Install it */
	KernelPageDirectory->pTables[0] = (PhysAddr_t)iTable | PAGE_USER | PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP;
	KernelPageDirectory->vTables[0] = (Addr_t)iTable;
	
	/* Init mutexes */
	MutexConstruct(&KernelPageDirectory->Lock);
	SpinlockReset(&VmLock);

	/* Map Memory Regions */

	/* HEAP */
	LogInformation("VMEM", "Mapping heap region to 0x%x", MEMORY_LOCATION_HEAP);
	MmVirtualIdentityMapMemoryRange(KernelPageDirectory, 0, MEMORY_LOCATION_HEAP,
		(MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP), 0, 0);

	/* VIDEO MEMORY (WITH FILL) */
	LogInformation("VMEM", "Mapping video memory to 0x%x", MEMORY_LOCATION_VIDEO);
	MmVirtualIdentityMapMemoryRange(KernelPageDirectory, GlbBootVideo.Info.FrameBufferAddr,
		MEMORY_LOCATION_VIDEO, (GlbBootVideo.Info.BytesPerScanline * GlbBootVideo.Info.Height), 
		1, PAGE_USER);

	/* Now, tricky, map reserved memory regions */

	/* Step 1. Install a pagetable at MEMORY_LOCATION_RESERVED */
	LogInformation("VMEM", "Mapping reserved memory to 0x%x", MEMORY_LOCATION_RESERVED);

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
	GlbBootVideo.Info.FrameBufferAddr = MEMORY_LOCATION_VIDEO;

	/* Last step is to mark all the memory region 
	 * where irq handlers reside for PAGE_USER 
	 * so user-space threads etc can enter irq handlers 
	 * and syscall */
	Addr_t IrqStart = (Addr_t)&exception_common;
	Addr_t IrqEnd = (Addr_t)&irq_handler255;

	/* One or two pages? */
	if ((IrqStart & PAGE_MASK) != (IrqEnd & PAGE_MASK)) {
		/* Two Pages */
		PageTable_t *pTableStart = 
			(PageTable_t*)KernelPageDirectory->vTables[PAGE_DIRECTORY_INDEX(IrqStart)];
		PageTable_t *pTableEnd = 
			(PageTable_t*)KernelPageDirectory->vTables[PAGE_DIRECTORY_INDEX(IrqEnd)];

		/* Pages */
		pTableStart->Pages[PAGE_TABLE_INDEX(IrqStart)] |= PAGE_USER;
		pTableEnd->Pages[PAGE_TABLE_INDEX(IrqEnd)] |= PAGE_USER;
	}
	else {
		/* One Page */
		PageTable_t *pTable = 
			(PageTable_t*)KernelPageDirectory->vTables[PAGE_DIRECTORY_INDEX(IrqStart)];
		pTable->Pages[PAGE_TABLE_INDEX(IrqStart)] |= PAGE_USER;
	}

	/* Enable paging */
	MmVirtualSwitchPageDirectory(0, KernelPageDirectory, (Addr_t)KernelPageDirectory);
	memory_set_paging(1);

	/* Setup initial Address Space */
	KernelAddrSpace.Flags = ADDRESS_SPACE_KERNEL;
	KernelAddrSpace.Cr3 = (Addr_t)KernelPageDirectory;
	KernelAddrSpace.PageDirectory = KernelPageDirectory;

	/* Setup */
	AddressSpaceInitKernel(&KernelAddrSpace);
}
