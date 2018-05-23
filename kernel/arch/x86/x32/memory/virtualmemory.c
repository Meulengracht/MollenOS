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
#define __MODULE		"VMEM"
//#define __TRACE

/* Includes
 * - System */
#include <component/cpu.h>
#include <system/addressspace.h>
#include <system/video.h>
#include <system/utils.h>
#include <threading.h>
#include <memory.h>
#include <debug.h>
#include <heap.h>
#include <arch.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Globals 
 * Needed for the virtual memory manager to keep
 * track of current directories */
static PageDirectory_t *KernelMasterTable   = NULL;
static Spinlock_t GlobalMemoryLock          = SPINLOCK_INIT;
static uintptr_t ReservedMemoryPointer      = 0;

/* Extern assembly functions that are
 * implemented in _paging.asm */
extern void memory_set_paging(int enable);
extern void memory_load_cr3(uintptr_t pda);
extern void memory_reload_cr3(void);
extern void memory_invalidate_addr(uintptr_t pda);
extern uint32_t memory_get_cr3(void);

/* MmVirtualCreatePageTable
 * Creates and initializes a new empty page-table */
PageTable_t*
MmVirtualCreatePageTable(void)
{
	// Variables
	PageTable_t *Table = NULL;
	PhysicalAddress_t Address = 0;

	// Allocate a new page-table instance
	Address = MmPhysicalAllocateBlock(MEMORY_ALLOCATION_MASK, 1);
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
	_In_ PageTable_t*       pTable, 
	_In_ PhysicalAddress_t  pAddressStart, 
	_In_ VirtualAddress_t   vAddressStart, 
	_In_ Flags_t            Flags)
{
	// Variables
	uintptr_t pAddress, vAddress;
	int i;

	// Iterate through pages and map them
	for (i = PAGE_TABLE_INDEX(vAddressStart), pAddress = pAddressStart, vAddress = vAddressStart;
		i < ENTRIES_PER_PAGE; i++, pAddress += PAGE_SIZE, vAddress += PAGE_SIZE) {
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
	_In_ PageDirectory_t*   PageDirectory,
	_In_ PhysicalAddress_t  pAddressStart,
	_In_ VirtualAddress_t   vAddressStart,
	_In_ uintptr_t          Length,
	_In_ int                Fill, 
	_In_ Flags_t            Flags)
{
	// Variables
	unsigned i, k;

	// Iterate the afflicted page-tables
	for (i = PAGE_DIRECTORY_INDEX(vAddressStart), k = 0;
		i < (PAGE_DIRECTORY_INDEX(vAddressStart + Length - 1) + 1);
		i++, k++) {
		PageTable_t *Table = MmVirtualCreatePageTable();
		uintptr_t pAddress = pAddressStart + (k * TABLE_SPACE_SIZE);
		uintptr_t vAddress = vAddressStart + (k * TABLE_SPACE_SIZE);

		// Fill it with pages?
		if (Fill != 0) {
			MmVirtualFillPageTable(Table, pAddress, vAddress, Flags);
		}

		// Install the table into the given page-directory
		PageDirectory->pTables[i] = (PhysicalAddress_t)Table 
			| (PAGE_SYSTEM_MAP | PAGE_PRESENT | PAGE_WRITE | Flags);
		PageDirectory->vTables[i] = (uintptr_t)Table;
	}
}

/* UpdateVirtualAddressingSpace
 * Switches page-directory for the current cpu instance */
OsStatus_t
UpdateVirtualAddressingSpace(
	_In_ void*              PageDirectory, 
	_In_ PhysicalAddress_t  Pdb)
{
	assert(PageDirectory != NULL && Pdb != 0);

    // Update current page-directory
	GetCurrentProcessorCore()->Data[0] = (uintptr_t)PageDirectory;
	memory_load_cr3(Pdb);
	return OsSuccess;
}

/* InitializeMemoryForApplicationCore
 * Initializes the missing memory setup for the calling cpu */
void
InitializeMemoryForApplicationCore(void)
{
    // Set current page-directory to kernel
    GetCurrentProcessorCore()->Data[0] = (uintptr_t)KernelMasterTable;
    memory_load_cr3((uintptr_t)KernelMasterTable);
    memory_set_paging(1);
}

/* MmVirtualSetFlags
 * Changes memory protection flags for the given virtual address */
OsStatus_t
MmVirtualSetFlags(
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   vAddress, 
	_In_ Flags_t            Flags)
{
	// Variabes
	PageDirectory_t *Directory  = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table          = NULL;
	int IsCurrent               = 0;

	// Determine page directory 
	// If we were given null, select the current
	if (Directory == NULL) {
		Directory = (PageDirectory_t*)GetCurrentProcessorCore()->Data[0];
        IsCurrent = 1;
	}
	else if ((PageDirectory_t*)GetCurrentProcessorCore()->Data[0] == Directory) {
		IsCurrent = 1;
	}
	assert(Directory != NULL);

	// Does page table exist?
	MutexLock(&Directory->Lock);
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] & PAGE_PRESENT)) {
        MutexUnlock(&Directory->Lock);
        return OsError;
	}
	else {
		Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)];
	}

	// Sanitize the table before we use it otherwise we might fuck up
	assert(Table != NULL);

	// Map it, make sure we mask the page address
	// so we don't accidently set any flags
	Table->Pages[PAGE_TABLE_INDEX(vAddress)] &= PAGE_MASK;
    Table->Pages[PAGE_TABLE_INDEX(vAddress)] |= (Flags & ATTRIBUTE_MASK);
	MutexUnlock(&Directory->Lock);

	// Last step is to invalidate the the address in the MMIO
	if (IsCurrent) {
		memory_invalidate_addr(vAddress);
	}
	return OsSuccess;
}

/* MmVirtualGetFlags
 * Retrieves memory protection flags for the given virtual address */
OsStatus_t
MmVirtualGetFlags(
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   vAddress, 
	_In_ Flags_t*           Flags)
{
	// Variabes
	PageDirectory_t *Directory  = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table          = NULL;
	int IsCurrent               = 0;

	// Determine page directory 
	// If we were given null, select the cuyrrent
	if (Directory == NULL) {
		Directory = (PageDirectory_t*)GetCurrentProcessorCore()->Data[0];
        IsCurrent = 1;
	}
	else if ((PageDirectory_t*)GetCurrentProcessorCore()->Data[0] == Directory) {
		IsCurrent = 1;
	}
	assert(Directory != NULL);

	// Does page table exist?
	MutexLock(&Directory->Lock);
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] & PAGE_PRESENT)) {
        MutexUnlock(&Directory->Lock);
        return OsError;
	}
	else {
		Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)];
	}

	// Sanitize the table before we use it otherwise we might fuck up
	assert(Table != NULL);

	// Map it, make sure we mask the page address
	// so we don't accidently set any flags
    if (Flags != NULL) {
        *Flags = Table->Pages[PAGE_TABLE_INDEX(vAddress)] & ATTRIBUTE_MASK;
    }
	MutexUnlock(&Directory->Lock);
	return OsSuccess;
}

/* MmVirtualMap
 * Installs a new page-mapping in the given
 * page-directory. The type of mapping is controlled by
 * the Flags parameter. */
OsStatus_t
MmVirtualMap(
	_In_ void*              PageDirectory, 
	_In_ PhysicalAddress_t  pAddress, 
	_In_ VirtualAddress_t   vAddress, 
	_In_ Flags_t            Flags)
{
	// Variabes
	OsStatus_t Result = OsSuccess;
	PageDirectory_t *Directory = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table = NULL;
	int IsCurrent = 0;

	// Determine page directory 
	// If we were given null, select the cuyrrent
	if (Directory == NULL) {
		Directory = (PageDirectory_t*)GetCurrentProcessorCore()->Data[0];
        IsCurrent = 1;
	}
	else if ((PageDirectory_t*)GetCurrentProcessorCore()->Data[0] == Directory) {
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
		uintptr_t Physical = 0;
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
			(uintptr_t)Table;

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
		FATAL(FATAL_SCOPE_KERNEL, 
			"Trying to remap virtual 0x%x to physical 0x%x (original mapping 0x%x)",
			vAddress, pAddress, Table->Pages[PAGE_TABLE_INDEX(vAddress)]);
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
	return Result;
}

/* MmVirtualUnmap
 * Unmaps a previous mapping from the given page-directory
 * the mapping must be present */
OsStatus_t
MmVirtualUnmap(
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   Address)
{
	// Variables needed for finding out page index
	PageDirectory_t *Directory  = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table          = NULL;
	OsStatus_t Result           = OsSuccess;
	int IsCurrent               = 0;

	// Determine page directory 
	// if pDir is null we get for current cpu
	if (Directory == NULL) {
		Directory = (PageDirectory_t*)GetCurrentProcessorCore()->Data[0];
        IsCurrent = 1;
	}
	else if ((PageDirectory_t*)GetCurrentProcessorCore()->Data[0] == Directory) {
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
		PhysicalAddress_t Physical = Table->Pages[PAGE_TABLE_INDEX(Address)];

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
	return Result;
}

/* MmVirtualGetMapping
 * Retrieves the physical address mapping of the
 * virtual memory address given - from the page directory 
 * that is given */
PhysicalAddress_t
MmVirtualGetMapping(
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   Address)
{
	// Initiate our variables
	PageDirectory_t *Directory  = (PageDirectory_t*)PageDirectory;
	PageTable_t *Table          = NULL;
	PhysicalAddress_t Mapping   = 0;

	// If none was given - use the current
	if (Directory == NULL) {
		Directory = (PageDirectory_t*)GetCurrentProcessorCore()->Data[0];
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

/* MmVirtualClone
 * Clones a new virtual memory space for an application to use. */
OsStatus_t
MmVirtualClone(
    _In_  int           Inherit,
    _Out_ void**        PageDirectory,
    _Out_ uintptr_t*    Pdb)
{
    // Variables
    uintptr_t PhysicalAddress   = 0;
    PageDirectory_t *NewPd      = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &PhysicalAddress);
    PageDirectory_t *CurrPd     = (PageDirectory_t*)GetCurrentProcessorCore()->Data[0];
    PageDirectory_t *KernPd     = KernelMasterTable;
    int Itr;

    // Copy at max kernel directories up to MEMORY_SEGMENT_RING3_BASE
    int KernelRegion            = 0;
    int KernelRegionEnd         = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_KERNEL_END);

    // Lookup which table-region is the stack region
    int ThreadRegion            = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int ThreadRegionEnd         = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_END);
	
    memset(NewPd, 0, sizeof(PageDirectory_t));
	MutexConstruct(&NewPd->Lock);

    // Initialize base mappings
    for (Itr = 0; Itr < ENTRIES_PER_PAGE; Itr++) {
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
        if (Inherit && CurrPd->pTables[Itr]) {
            NewPd->pTables[Itr] = CurrPd->pTables[Itr] | PAGE_INHERITED;
            NewPd->vTables[Itr] = CurrPd->vTables[Itr];
        }
    }

    // Update out's
    *PageDirectory  = (void*)NewPd;
    *Pdb            = PhysicalAddress;
    return OsSuccess;
}

/* MmVirtualDestroy
 * Destroys and cleans up any resources used by the virtual address space. */
OsStatus_t
MmVirtualDestroy(
	_In_ void*          PageDirectory)
{
    // Variables
    PageDirectory_t *Pd     = (PageDirectory_t*)PageDirectory;
    PageDirectory_t *KernPd = KernelMasterTable;
    int i, j;

    // Iterate page-mappings
    for (i = 0; i < ENTRIES_PER_PAGE; i++) {
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
        for (j = 0; j < ENTRIES_PER_PAGE; j++) {
            if (Pt->Pages[j] & PAGE_VIRTUAL) {
                continue;
            }

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
    
    // Free the page-directory
    kfree(Pd);
    return OsSuccess;
}

/* MmVirtualInitialMap
 * Maps a virtual memory address to a physical
 * memory address in a given page-directory
 * If page-directory is NULL, current directory
 * is used */
void 
MmVirtualInitialMap(
	_In_ PhysicalAddress_t  pAddress, 
	_In_ VirtualAddress_t   vAddress)
{
	// Variables
	PageDirectory_t *Directory  = KernelMasterTable;
	PageTable_t *Table          = NULL;

	// If table is not present in directory
	// we must allocate a new one and install it
	if (!(Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] & PAGE_PRESENT)) {
		Table = MmVirtualCreatePageTable();
		Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] = (PhysicalAddress_t)Table
			| PAGE_PRESENT | PAGE_WRITE;
		Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)] = (PhysicalAddress_t)Table;
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
VirtualAddress_t*
MmReserveMemory(
	_In_ int Pages)
{
	// Variables
	VirtualAddress_t ReturnAddress = 0;

	// Calculate new address 
	// this is a locked operation
	SpinlockAcquire(&GlobalMemoryLock);
	ReturnAddress = ReservedMemoryPointer;
	ReservedMemoryPointer += (PAGE_SIZE * Pages);
	SpinlockRelease(&GlobalMemoryLock);
	return (VirtualAddress_t*)ReturnAddress;
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

	// Trace information
	TRACE("MmVirtualInit()");

	// Initialize reserved pointer
	ReservedMemoryPointer = MEMORY_LOCATION_RESERVED;

	// Allocate 3 pages for the kernel page directory
	// and reset it by zeroing it out
	KernelMasterTable = (PageDirectory_t*)
		MmPhysicalAllocateBlock(MEMORY_ALLOCATION_MASK, 3);
	memset((void*)KernelMasterTable, 0, sizeof(PageDirectory_t));

	// Allocate initial page directory and
	// identify map the page-directory
	iTable = MmVirtualCreatePageTable();
	MmVirtualFillPageTable(iTable, 0x1000, 0x1000, 0);

	// Install the first page-table
	KernelMasterTable->pTables[0] = (PhysicalAddress_t)iTable | PAGE_USER
		| PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP;
	KernelMasterTable->vTables[0] = (uintptr_t)iTable;
	
	// Initialize locks
	MutexConstruct(&KernelMasterTable->Lock);
	SpinlockReset(&GlobalMemoryLock);

	// Pre-map heap region
	TRACE("Mapping heap region to 0x%x", MEMORY_LOCATION_HEAP);
	MmVirtualIdentityMapMemoryRange(KernelMasterTable, 0, MEMORY_LOCATION_HEAP,
		(MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP), 0, 0);

	// Pre-map video region
	TRACE("Mapping video memory to 0x%x", MEMORY_LOCATION_VIDEO);
	MmVirtualIdentityMapMemoryRange(KernelMasterTable, VideoGetTerminal()->FrameBufferAddress,
		MEMORY_LOCATION_VIDEO, (VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height),
		1, PAGE_USER);

	// Install the page table at the reserved system
	// memory, important! 
	TRACE("Mapping reserved memory to 0x%x", MEMORY_LOCATION_RESERVED);

	// Update video address to the new
	VideoGetTerminal()->FrameBufferAddress = MEMORY_LOCATION_VIDEO;

	// Update and switch page-directory for boot-cpu
	UpdateVirtualAddressingSpace((void*)KernelMasterTable, (uintptr_t)KernelMasterTable);
	memory_set_paging(1);

	// Setup kernel addressing space
	KernelSpace.Flags                       = ASPACE_TYPE_KERNEL;
	KernelSpace.Data[ASPACE_DATA_CR3]       = (uintptr_t)KernelMasterTable;
	KernelSpace.Data[ASPACE_DATA_PDPOINTER] = (uintptr_t)KernelMasterTable;
	return AddressSpaceInitialize(&KernelSpace);
}
