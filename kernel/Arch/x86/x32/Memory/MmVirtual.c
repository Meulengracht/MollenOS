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
#include <Log.h>

#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Globals */
PageDirectory_t *KernelPageDirectory = NULL;
PageDirectory_t *CurrentPageDirectories[64];
AddressSpace_t KernelAddressSpace;
volatile Addr_t GblReservedPtr = 0;

/* Lock */
Spinlock_t VmLock;

/* Externs */
extern volatile uint32_t GlbNumLogicalCpus;
extern Graphics_t GfxInformation;
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
	PhysAddr_t pAddr = MmPhysicalAllocateBlock();
	PageTable_t *pTable = (PageTable_t*)pAddr;

	/* Sanity */
	assert((PhysAddr_t)pTable > 0);

	/* Zero it */
	memset((void*)pTable, 0, sizeof(PageTable_t));

	/* Done */
	return pTable;
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
		PageTable_t *pTable = MmVirtualCreatePageTable();

		/* Get addresses that match page table */
		uint32_t CurrPhys = PhysStart + (k * TABLE_SPACE_SIZE);
		uint32_t CurrVirt = VirtStart + (k * TABLE_SPACE_SIZE);

		/* Fill it */
		if (Fill != 0)
			MmVirtualFillPageTable(pTable, CurrPhys, CurrVirt);

		/* Install Table */
		PageDirectory->pTables[i] = (PhysAddr_t)pTable | PAGE_PRESENT | PAGE_WRITE | Flags;
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
	/* Vars */
	PageDirectory_t *pdir = (PageDirectory_t*)PageDirectory;
	PageTable_t *ptable = NULL;

	/* Determine page directory */
	if (pdir == NULL)
		pdir = (PageDirectory_t*)CurrentPageDirectories[ApicGetCpu()];

	/* Sanity */
	assert(pdir != NULL);

	/* Get lock */
	CriticalSectionEnter(&pdir->Lock);

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* No... Create it */
		Addr_t TablePhys = 0;
		PageTable_t *nTable = NULL;

		/* Allocate new table */
		nTable = (PageTable_t*)kmalloc_ap(PAGE_SIZE, &TablePhys);

		/* Sanity */
		assert((Addr_t)nTable > 0);

		/* Zero it */
		memset((void*)nTable, 0, sizeof(PageTable_t));

		/* Install it */
		pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = TablePhys | PAGE_PRESENT | PAGE_WRITE | Flags;
		pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] = (Addr_t)nTable;

		/* Reload CR3 */
		if (PageDirectory == NULL)
			memory_reload_cr3();
	}

	/* Get it */
	ptable = (PageTable_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Now, lets map page! */
	if (ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] != 0)
	{
		LogFatal("VMEM", "Trying to remap virtual 0x%x to physical 0x%x (original mapping 0x%x)",
			VirtualAddr, PhysicalAddr, ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)]);
		for (;;);
	}

	/* Map it */
	ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] = (PhysicalAddr & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | Flags;

	/* Release lock */
	CriticalSectionLeave(&pdir->Lock);

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
	CriticalSectionEnter(&pdir->Lock);

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* No... What the fuck? */
		
		/* Release mutex */
		CriticalSectionLeave(&pdir->Lock);

		/* Return */
		return;
	}

	/* Get it */
	ptable = (PageTable_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Sanity */
	if (ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] == 0)
	{
		/* Release mutex */
		CriticalSectionLeave(&pdir->Lock);

		/* Return */
		return;
	}

	/* Do it */
	phys = ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)];
	ptable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] = 0;

	/* Release memory */
	MmPhysicalFreeBlock(phys);

	/* Release mutex */
	CriticalSectionLeave(&pdir->Lock);

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
	/* Vars */
	PageDirectory_t *pDir = (PageDirectory_t*)PageDirectory;
	PageTable_t *pTable = NULL;
	PhysAddr_t PhysMap = 0;

	/* Determine page directory */
	if (pDir == NULL)
		pDir = (PageDirectory_t*)CurrentPageDirectories[ApicGetCpu()];

	/* Sanity */
	assert(pDir != NULL);

	/* Get mutex */
	CriticalSectionEnter(&pDir->Lock);

	/* Does page table exist? */
	if (!(pDir->pTables[PAGE_DIRECTORY_INDEX(VirtualAddr)] & PAGE_PRESENT))
	{
		/* Release mutex */
		CriticalSectionLeave(&pDir->Lock);

		/* Return */
		return PhysMap;
	}

	/* Get it */
	pTable = (PageTable_t*)pDir->vTables[PAGE_DIRECTORY_INDEX(VirtualAddr)];

	/* Sanity */
	assert(pTable != NULL);

	/* Return mapping */
	PhysMap = pTable->Pages[PAGE_TABLE_INDEX(VirtualAddr)] & PAGE_MASK;

	/* Release mutex */
	CriticalSectionLeave(&pDir->Lock);

	/* Sanity */
	if (PhysMap == 0)
		return PhysMap;

	/* Done - Return with offset */
	return (PhysMap + (VirtualAddr & ATTRIBUTE_MASK));
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
VirtAddr_t *MmVirtualMapSysMemory(PhysAddr_t PhysicalAddr, int Pages)
{
	int i;
	VirtAddr_t ret = 0;

	/* Acquire Lock */
	SpinlockAcquire(&VmLock);

	/* This is the addr that we return */
	ret = GblReservedPtr;

	/* Map it */
	for (i = 0; i < Pages; i++)
	{
		/* Call Map on kernel directory */
		if (!MmVirtualGetMapping(NULL, GblReservedPtr))
			MmVirtualMap(NULL, PhysicalAddr + (i * PAGE_SIZE), GblReservedPtr, 0);

		/* Increase */
		GblReservedPtr += PAGE_SIZE;
	}

	/* Release */
	SpinlockRelease(&VmLock);

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
	GlbNumLogicalCpus = 0;
	GblReservedPtr = MEMORY_LOCATION_RESERVED;

	/* Info */
	LogInformation("VMEM", "Initializing");

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
	CriticalSectionConstruct(&KernelPageDirectory->Lock);
	SpinlockReset(&VmLock);

	/* Map Memory Regions */

	/* HEAP */
	LogInformation("VMEM", "Mapping heap region to 0x%x", MEMORY_LOCATION_HEAP);
	MmVirtualIdentityMapMemoryRange(KernelPageDirectory, 0, MEMORY_LOCATION_HEAP,
		(MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP), 0, 0);

	/* VIDEO MEMORY (WITH FILL) */
	LogInformation("VMEM", "Mapping video memory to 0x%x", MEMORY_LOCATION_VIDEO);
	MmVirtualIdentityMapMemoryRange(KernelPageDirectory, GfxInformation.VideoAddr,
		MEMORY_LOCATION_VIDEO, (GfxInformation.BytesPerScanLine * GfxInformation.ResY), 1, PAGE_USER);

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
	GfxInformation.VideoAddr = MEMORY_LOCATION_VIDEO;

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
	KernelAddressSpace.Flags = ADDRESS_SPACE_KERNEL;
	KernelAddressSpace.Cr3 = (Addr_t)KernelPageDirectory;
	KernelAddressSpace.PageDirectory = KernelPageDirectory;
}

/* Address Space Abstraction Layer
 **********************************/
AddressSpace_t *AddressSpaceCreate(uint32_t Flags)
{
	/* Allocate Structure */
	AddressSpace_t *AddrSpace = (AddressSpace_t*)kmalloc(sizeof(AddressSpace_t));
	Cpu_t CurrentCpu = ApicGetCpu();
	uint32_t Itr = 0;

	/* Save Flags */
	AddrSpace->Flags = Flags;

	/* Depends on what caller wants */
	if (Flags & ADDRESS_SPACE_KERNEL)
	{
		/* Get kernel Space */
		AddrSpace->Cr3 = (Addr_t)KernelPageDirectory;
		AddrSpace->PageDirectory = KernelPageDirectory;
	}
	else if (Flags & ADDRESS_SPACE_INHERIT)
	{
		/* Get Current Space */
		AddrSpace->Cr3 = memory_get_cr3();
		AddrSpace->PageDirectory = MmVirtualGetCurrentDirectory(CurrentCpu);
	}
	else if (Flags & ADDRESS_SPACE_USER)
	{
		/* Create new */
		Addr_t PhysAddr = 0;
		PageDirectory_t *NewPd = (PageDirectory_t*)kmalloc_p(sizeof(PageDirectory_t), &PhysAddr);

		/* Start out by resetting all */
		memset(NewPd, 0, sizeof(PageDirectory_t));

		/* Setup Lock */
		CriticalSectionConstruct(&NewPd->Lock);

		/* Map in kernel space */
		for (Itr = 0; Itr < 512; Itr++)
		{
			NewPd->pTables[Itr] = KernelPageDirectory->pTables[Itr];
			NewPd->vTables[Itr] = KernelPageDirectory->vTables[Itr];
		}

		/* Set */
		AddrSpace->Cr3 = PhysAddr;
		AddrSpace->PageDirectory = NewPd;
	}
	else
		LogFatal("VMEM", "Invalid flags parsed in AddressSpaceCreate 0x%x", Flags);

	/* Done */
	return AddrSpace;
}

/* Destroy and release all resources */
void AddressSpaceDestroy(AddressSpace_t *AddrSpace)
{
	_CRT_UNUSED(AddrSpace);
}

/* Get Current Address Space */
AddressSpace_t *AddressSpaceGetCurrent(void)
{
	/* Get current thread */
	Cpu_t CurrentCpu = ApicGetCpu();
	MCoreThread_t *CurrThread = ThreadingGetCurrentThread(CurrentCpu);

	/* Sanity */
	if (CurrThread == NULL)
		return &KernelAddressSpace;
	else
		return CurrThread->AddrSpace;
}

/* Switch to given address space */
void AddressSpaceSwitch(AddressSpace_t *AddrSpace)
{
	/* Get current cpu */
	Cpu_t CurrentCpu = ApicGetCpu();

	/* Deep Call */
	MmVirtualSwitchPageDirectory(CurrentCpu, AddrSpace->PageDirectory, AddrSpace->Cr3);
}

/* Unmaps kernel space from the given Address Space */
void AddressSpaceReleaseKernel(AddressSpace_t *AddrSpace)
{
	/* Vars */
	PageDirectory_t *Pd = (PageDirectory_t*)AddrSpace->PageDirectory;
	uint32_t Itr = 0;

	/* Now unmap */
	for (Itr = 0; Itr < 512; Itr++)
	{
		Pd->pTables[Itr] = 0;
		Pd->vTables[Itr] = 0;
	}
}

/* Map a virtual address into the Address Space */
void AddressSpaceMap(AddressSpace_t *AddrSpace, VirtAddr_t Address, int UserMode)
{
	/* Deep Call */
	MmVirtualMap(AddrSpace->PageDirectory, MmPhysicalAllocateBlock(), 
		Address, UserMode != 0 ? PAGE_USER : 0);
}

/* Map a virtual address to a fixed physical page */
void AddressSpaceMapFixed(AddressSpace_t *AddrSpace,
	PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, int UserMode)
{
	/* Deep Call */
	MmVirtualMap(AddrSpace->PageDirectory, PhysicalAddr,
		VirtualAddr, UserMode != 0 ? PAGE_USER : 0);
}

/* Unmaps a virtual page from an address space */
void AddressSpaceUnmap(AddressSpace_t *AddrSpace, VirtAddr_t Address)
{
	/* Deep Call */
	MmVirtualUnmap(AddrSpace->PageDirectory, Address);
}

/* Retrieves a physical mapping from an address space */
PhysAddr_t AddressSpaceGetMap(AddressSpace_t *AddrSpace, VirtAddr_t Address)
{
	/* Deep Call */
	return MmVirtualGetMapping(AddrSpace->PageDirectory, Address);
}