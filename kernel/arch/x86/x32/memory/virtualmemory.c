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
 * MollenOS x86-32 Virtual Memory Manager
 * - Contains the implementation of virtual memory management
 *   for the X86-32 Architecture 
 */

/* Includes
 * - System */
#include <system/addresspace.h>
#include <system/video.h>
#include <process/server.h>
#include <threading.h>
#include <heap.h>
#include <video.h>
#include <memory.h>
#include <log.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Globals 
 * Needed for the virtual memory manager to keep
 * track of current directories */
static PageDirectory_t *GlbKernelPageDirectory = NULL;
static PageDirectory_t *GlbPageDirectories[MAX_SUPPORTED_CPUS];
static Spinlock_t GlbVmLock = SPINLOCK_INIT;
static Addr_t GblReservedPtr = 0;

/* Extern acess to system mappings in the
 * physical memory manager */
__EXTERN SystemMemoryMapping_t SysMappings[32];
//__EXTERN MCoreVideoDevice_t GlbBootVideo;

/* Extern assembly functions that are
 * implemented in _paging.asm */
__EXTERN void memory_set_paging(int enable);
__EXTERN void memory_load_cr3(Addr_t pda);
__EXTERN void memory_reload_cr3(void);
__EXTERN void memory_invalidate_addr(Addr_t pda);
__EXTERN uint32_t memory_get_cr3(void);

/* MmVirtualCreatePageTable
 * Creates and initializes a new empty page-table */
PageTable_t*
MmVirtualCreatePageTable(void)
{
	// Variables
	PageTable_t *Table = NULL;
	PhysAddr_t Address = 0;

	// Allocate a new page-table instance
	Address = MmPhysicalAllocateBlock(MEMORY_INIT_MASK, 1);
	Table = (PageTable_t*)Address;

	// Make sure all is good
	assert(Table != NULL);

	// Initialize it and return
	memset((void*)Table, 0, sizeof(PageTable_t));
	return Table;
}

/* MmVirtualFillPageTable
 * Identity maps a memory region inside the given
 * page-table - type of mappings is controlled with Flags */
void 
MmVirtualFillPageTable(
	_Out_ PageTable_t *pTable, 
	_In_ PhysAddr_t pAddressStart, 
	_In_ VirtAddr_t vAddressStart, 
	_In_ Flags_t Flags)
{
	// Variables
	Addr_t pAddress, vAddress;
	int i;

	// Iterate through pages and map them
	for (i = PAGE_TABLE_INDEX(vAddressStart), pAddress = pAddressStart, vAddress = vAddressStart;
		i < 1024;
		i++, pAddress += PAGE_SIZE, vAddress += PAGE_SIZE) {
		uint32_t pEntry = pAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP | Flags;
		pTable->Pages[PAGE_TABLE_INDEX(vAddress)] = pEntry;
	}
}

/* MmVirtualIdentityMapMemoryRange
 * Identity maps not only a page-table or a region inside
 * it can identity map an entire memory region and create
 * page-table for the region automatically */
void 
MmVirtualIdentityMapMemoryRange(
	_Out_ PageDirectory_t* PageDirectory,
	_In_ PhysAddr_t pAddressStart, 
	_In_ VirtAddr_t vAddressStart, 
	_In_ Addr_t Length, 
	_In_ int Fill, 
	_In_ Flags_t Flags)
{
	// Variables
	int i, k;

	// Iterate the afflicted page-tables
	for (i = PAGE_DIRECTORY_INDEX(vAddressStart), k = 0;
		i < (PAGE_DIRECTORY_INDEX(vAddressStart + Length - 1) + 1);
		i++, k++) {
		PageTable_t *Table = MmVirtualCreatePageTable();
		Addr_t pAddress = pAddressStart + (k * TABLE_SPACE_SIZE);
		Addr_t vAddress = vAddressStart + (k * TABLE_SPACE_SIZE);

		// Fill it with pages?
		if (Fill != 0) {
			MmVirtualFillPageTable(Table, pAddress, vAddress, Flags);
		}

		// Install the table into the given page-directory
		PageDirectory->pTables[i] = (PhysAddr_t)Table 
			| (PAGE_SYSTEM_MAP | PAGE_PRESENT | PAGE_WRITE | Flags);
		PageDirectory->vTables[i] = (Addr_t)Table;
	}
}

/* MmVirtualSwitchPageDirectory
 * Switches page-directory for the current cpu
 * but the current cpu should be given as parameter
 * as well */
OsStatus_t
MmVirtualSwitchPageDirectory(
	_In_ UUId_t Cpu, 
	_In_ PageDirectory_t* PageDirectory, 
	_In_ PhysAddr_t Pdb)
{
	// Sanitize the parameter
	assert(PageDirectory != NULL);

	// Update current and load the page-directory
	GlbPageDirectories[Cpu] = PageDirectory;
	memory_load_cr3(Pdb);

	// Done - no errors
	return OsNoError;
}

/* MmVirtualGetCurrentDirectory
 * Retrieves the current page-directory for the given cpu */
PageDirectory_t*
MmVirtualGetCurrentDirectory(
	_In_ UUId_t Cpu)
{
	// Sanitize
	assert(Cpu < MAX_SUPPORTED_CPUS);

	// Return the current - even if null
	return GlbPageDirectories[Cpu];
}

/* MmVirtualInstallPaging
 * Initializes paging for the given cpu id */
OsStatus_t
MmVirtualInstallPaging(
	_In_ UUId_t Cpu)
{
	MmVirtualSwitchPageDirectory(Cpu, GlbKernelPageDirectory, 
		(Addr_t)GlbKernelPageDirectory);
	memory_set_paging(1);
}

/* MmVirtualMap
 * Installs a new page-mapping in the given
 * page-directory. The type of mapping is controlled by
 * the Flags parameter. */
OsStatus_t
MmVirtualMap(
	_In_ void *PageDirectory, 
	_In_ PhysAddr_t pAddress, 
	_In_ VirtAddr_t vAddress, 
	_In_ Flags_t Flags)
{
	// Variabes
	OsStatus_t Result = OsNoError;
	PageDirectory_t *Directory = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table = NULL;
	int IsCurrent = 0;

	// Determine page directory 
	// If we were given null, select the cuyrrent
	if (Directory == NULL) {
		Directory = GlbPageDirectories[ApicGetCpu()];
	}

	// Sanitizie if we are modifying the currently
	// loaded page-directory
	if (GlbPageDirectories[ApicGetCpu()] == Directory) {
		IsCurrent = 1;
	}

	// Sanitize again
	// If its still null something is wrong
	assert(Directory != NULL);

	// Get lock on the page-directory 
	// we don't want people to touch 
	MutexLock(&Directory->Lock);

	// Does page table exist? 
	// If the page-table is not even mapped in we need to 
	// do that beforehand
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] & PAGE_PRESENT)) {
		Addr_t Physical = 0;
		Table = (PageTable_t*)kmalloc_ap(PAGE_SIZE, &Physical);

		// Sanitize the newly allocated table
		// and then initialize the table
		assert(Table != NULL);
		memset((void*)Table, 0, sizeof(PageTable_t));

		// Install it into our directory, now if the address
		// we are mapping is user-accessible, we should add flags
		Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] =
			Physical | PAGE_PRESENT | PAGE_WRITE | Flags;
		Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)] =
			(Addr_t)Table;

		// Reload CR3 directory to force 
		// the MMIO to see our changes 
		if (IsCurrent) {
			memory_reload_cr3();
		}
	}
	else {
		Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)];
	}

	// Sanitize the table before we use it 
	// otherwise we might fuck up
	assert(Table != NULL);

	// Sanitize that the index isn't already
	// mapped in, thats a fatality
	if (Table->Pages[PAGE_TABLE_INDEX(vAddress)] != 0) {
		LogFatal("VMEM", "Trying to remap virtual 0x%x to physical 0x%x (original mapping 0x%x)",
			vAddress, pAddress, Table->Pages[PAGE_TABLE_INDEX(vAddress)]);
		kernel_panic("This is not good");
	}

	// Map it, make sure we mask the page address
	// so we don't accidently set any flags
	Table->Pages[PAGE_TABLE_INDEX(vAddress)] =
		(pAddress & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | Flags;

	// Unlock
	MutexUnlock(&Directory->Lock);

	// Last step is to invalidate the 
	// the address in the MMIO
	if (IsCurrent) {
		memory_invalidate_addr(vAddress);
	}

	// Done - no errors
	return OsNoError;
}

/* MmVirtualUnmap
 * Unmaps a previous mapping from the given page-directory
 * the mapping must be present */
OsStatus_t
MmVirtualUnmap(
	_In_ void *PageDirectory, 
	_In_ VirtAddr_t Address)
{
	// Variables needed for finding out page index
	OsStatus_t Result = OsNoError;
	PageDirectory_t *Directory = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table = NULL;
	int IsCurrent = 0;

	// Determine page directory 
	// if pDir is null we get for current cpu
	if (Directory == NULL) {
		Directory = GlbPageDirectories[ApicGetCpu()];
	}

	// Sanitizie if we are modifying the currently
	// loaded page-directory
	if (GlbPageDirectories[ApicGetCpu()] == Directory) {
		IsCurrent = 1;
	}

	// Sanitize the page-directory
	// If it's still NULL somethings wrong
	assert(Directory != NULL);

	// Acquire the mutex
	MutexLock(&Directory->Lock);

	// Does page table exist? 
	// or is a system table, we can't unmap these!
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(Address)] & PAGE_PRESENT)
		|| (Directory->pTables[PAGE_DIRECTORY_INDEX(Address)] & PAGE_SYSTEM_MAP)) {
		Result = OsError;
		goto Leave;
	}

	/* Acquire the proper page-table */
	Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(Address)];

	// Sanitize the page-index, if it's not mapped in
	// then we are trying to unmap somethings that not even mapped
	assert(Table->Pages[PAGE_TABLE_INDEX(Address)] != 0);

	// System memory? Don't unmap, for gods sake
	if (Table->Pages[PAGE_TABLE_INDEX(Address)] & PAGE_SYSTEM_MAP) {
		Result = OsError;
		goto Leave;
	}
	else
	{
		// Ok, step one is to extract the physical page of this index
		PhysAddr_t Physical = Table->Pages[PAGE_TABLE_INDEX(Address)];

		// Clear the mapping out
		Table->Pages[PAGE_TABLE_INDEX(Address)] = 0;

		// Release memory, but don't if it 
		// is a virtual mapping, that means we should not free
		// the physical page
		if (!(Physical & PAGE_VIRTUAL)) {
			MmPhysicalFreeBlock(Physical & PAGE_MASK);
		}

		// Last step is to validate the page-mapping
		// now this should be an IPC to all cpu's
		if (IsCurrent) {
			memory_invalidate_addr(Address);
		}
	}

Leave:
	// Release the mutex and allow 
	// others to use the page-directory
	MutexUnlock(&Directory->Lock);

	// Done - return error code
	return Result;
}

/* MmVirtualGetMapping
 * Retrieves the physical address mapping of the
 * virtual memory address given - from the page directory 
 * that is given */
PhysAddr_t
MmVirtualGetMapping(
	_In_ void *PageDirectory, 
	_In_ VirtAddr_t Address)
{
	// Initiate our variables
	PageDirectory_t *Directory = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table = NULL;
	PhysAddr_t Mapping = 0;

	// If none was given - use the current
	if (Directory == NULL) {
		Directory = GlbPageDirectories[ApicGetCpu()];
	}

	// Sanitize the page-directory
	// If it's still NULL somethings wrong
	assert(Directory != NULL);

	// Acquire lock for this directory
	MutexLock(&Directory->Lock);

	// Is the table even present in the directory? 
	// If not, then no mapping 
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(Address)] & PAGE_PRESENT)) {
		goto NotMapped;
	}

	// Fetch the page table from the page-directory
	Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(Address)];

	/* Sanitize the page-table just in case */
	assert(Table != NULL);

	// Sanitize the mapping before anything
	if (!(Table->Pages[PAGE_TABLE_INDEX(Address)] & PAGE_PRESENT)) {
		goto NotMapped;
	}

	// Retrieve mapping
	Mapping = Table->Pages[PAGE_TABLE_INDEX(Address)] & PAGE_MASK;

	// Release mutex on page-directory
	// we should not keep it longer than neccessary
	MutexUnlock(&Directory->Lock);

	// Done - Return with offset
	return (Mapping + (Address & ATTRIBUTE_MASK));

NotMapped:
	// On fail - release and return 0
	MutexUnlock(&Directory->Lock);
	return 0;
}

/* MmVirtualInitialMap
 * Maps a virtual memory address to a physical
 * memory address in a given page-directory
 * If page-directory is NULL, current directory
 * is used */
void 
MmVirtualInitialMap(
	_In_ PhysAddr_t pAddress, 
	_In_ VirtAddr_t vAddress)
{
	// Variables
	PageDirectory_t *Directory = GlbKernelPageDirectory;
	PageTable_t *Table = NULL;

	// If table is not present in directory
	// we must allocate a new one and install it
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] & PAGE_PRESENT)) {
		Table = MmVirtualCreatePageTable();
		Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] = (PhysAddr_t)Table
			| PAGE_PRESENT | PAGE_WRITE;
		Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)] = (PhysAddr_t)Table;
	}
	else {
		Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)];
	}

	// Sanitize no previous mapping exists
	assert(Table->Pages[PAGE_TABLE_INDEX(vAddress)] == 0
		&& "Dont remap pages without freeing :(");

	// Install the mapping
	Table->Pages[PAGE_TABLE_INDEX(vAddress)] =
		(pAddress & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE;
}

/* MmReserveMemory
 * Reserves memory for system use - should be allocated
 * from a fixed memory region that won't interfere with
 * general usage */
VirtAddr_t*
MmReserveMemory(
	_In_ int Pages)
{
	// Variables
	VirtAddr_t ReturnAddress = 0;

	// Calculate new address 
	// this is a locked operation
	SpinlockAcquire(&GlbVmLock);
	ReturnAddress = GblReservedPtr;
	GblReservedPtr += (PAGE_SIZE * Pages);
	SpinlockRelease(&GlbVmLock);

	// Done - return address
	return (VirtAddr_t*)ReturnAddress;
}

/* MmVirtualInit
 * Initializes the virtual memory system and
 * installs default kernel mappings */
OsStatus_t
MmVirtualInit(void)
{
	// Variables
	AddressSpace_t KernelSpace;
	PageTable_t *iTable = NULL;
	int i;

	// Trace information
	LogInformation("VMEM", "Initializing");

	// Initialize reserved pointer
	GblReservedPtr = MEMORY_LOCATION_RESERVED;

	// Allocate 3 pages for the kernel page directory
	// and reset it by zeroing it out
	GlbKernelPageDirectory = (PageDirectory_t*)
		MmPhysicalAllocateBlock(MEMORY_INIT_MASK, 3);
	memset((void*)GlbKernelPageDirectory, 0, sizeof(PageDirectory_t));

	// Allocate initial page directory and
	// identify map the page-directory
	iTable = MmVirtualCreatePageTable();
	MmVirtualFillPageTable(iTable, 0x1000, 0x1000, 0);

	// Install the first page-table
	GlbKernelPageDirectory->pTables[0] = (PhysAddr_t)iTable | PAGE_USER
		| PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP;
	GlbKernelPageDirectory->vTables[0] = (Addr_t)iTable;
	
	// Initialize locks
	MutexConstruct(&GlbKernelPageDirectory->Lock);
	SpinlockReset(&GlbVmLock);

	// Pre-map heap region
	LogInformation("VMEM", "Mapping heap region to 0x%x", MEMORY_LOCATION_HEAP);
	MmVirtualIdentityMapMemoryRange(GlbKernelPageDirectory, 0, MEMORY_LOCATION_HEAP,
		(MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP), 0, 0);

	// Pre-map video region
	LogInformation("VMEM", "Mapping video memory to 0x%x", MEMORY_LOCATION_VIDEO);
	MmVirtualIdentityMapMemoryRange(GlbKernelPageDirectory, VideoGetTerminal()->Info.FrameBufferAddress,
		MEMORY_LOCATION_VIDEO, (VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height),
		1, PAGE_USER);

	// Install the page table at the reserved system
	// memory, important! 
	LogInformation("VMEM", "Mapping reserved memory to 0x%x", MEMORY_LOCATION_RESERVED);

	// Iterate all saved physical system memory mappings
	for (i = 0; i < 32; i++) {
		if (SysMappings[i].Length != 0 && SysMappings[i].Type != 1) {
			int PageCount = DIVUP(SysMappings[i].Length, PAGE_SIZE);
			int j;

			// Update virtual address for this entry
			SysMappings[i].vAddressStart = GblReservedPtr;

			// Map it with our special initial mapping function
			// that is a simplified version
			for (j = 0; j < PageCount; j++, GblReservedPtr += PAGE_SIZE) {
				MmVirtualInitialMap(
					((SysMappings[i].pAddressStart & PAGE_MASK) + (j * PAGE_SIZE)), 
					GblReservedPtr);
			}
		}
	}

	// Update video address to the new
	VideoGetTerminal()->Info.FrameBufferAddress = MEMORY_LOCATION_VIDEO;

	// Update and switch page-directory for boot-cpu
	MmVirtualSwitchPageDirectory(0, GlbKernelPageDirectory, (Addr_t)GlbKernelPageDirectory);
	memory_set_paging(1);

	// Setup kernel addressing space
	KernelSpace.Flags = AS_TYPE_KERNEL;
	KernelSpace.Cr3 = (Addr_t)GlbKernelPageDirectory;
	KernelSpace.PageDirectory = GlbKernelPageDirectory;

	// Done! 
	return AddressSpaceInitKernel(&KernelSpace);
}
