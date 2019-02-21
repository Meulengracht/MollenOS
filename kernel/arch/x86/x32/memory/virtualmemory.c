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

#include <component/cpu.h>
#include <memoryspace.h>
#include <system/output.h>
#include <system/utils.h>
#include <threading.h>
#include <machine.h>
#include <handle.h>
#include <memory.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <arch.h>
#include <cpu.h>
#include <gdt.h>

/* Extern assembly functions that are
 * implemented in _paging.asm */
extern OsStatus_t SwitchVirtualSpace(SystemMemorySpace_t*);
extern void memory_set_paging(int enable);
extern void memory_reload_cr3(void);

/* MmVirtualCreatePageTable
 * Creates and initializes a new empty page-table */
PageTable_t*
MmVirtualCreatePageTable(void)
{
	// Variables
	PageTable_t *Table = NULL;
	PhysicalAddress_t Address = 0;

	// Allocate a new page-table instance
	Address = AllocateSystemMemory(sizeof(PageTable_t), MEMORY_ALLOCATION_MASK, 0);
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
    atomic_uint* AtomicPointer;
	uintptr_t    pAddress, vAddress;
	int          i;

	// Iterate through pages and map them
	for (i = PAGE_TABLE_INDEX(vAddressStart), pAddress = pAddressStart, vAddress = vAddressStart;
		i < ENTRIES_PER_PAGE; i++, pAddress += PAGE_SIZE, vAddress += PAGE_SIZE) {
        AtomicPointer = &pTable->Pages[PAGE_TABLE_INDEX(vAddress)];
        atomic_store_explicit(AtomicPointer, pAddress | Flags, memory_order_relaxed);
	}
}

/* MmVirtualMapMemoryRange
 * Maps an entire region into the given page-directory and marks them present.
 * They can then be used for either identity mapping or real mappings afterwards. */
void 
MmVirtualMapMemoryRange(
	_In_ PageDirectory_t*   PageDirectory,
	_In_ VirtualAddress_t   AddressStart,
	_In_ uintptr_t          Length,
	_In_ Flags_t            Flags)
{
    atomic_uint* AtomicPointer;
	unsigned     i;

	// Iterate the afflicted page-tables
	for (i = PAGE_DIRECTORY_INDEX(AddressStart);
		i < (PAGE_DIRECTORY_INDEX(AddressStart + Length - 1) + 1); i++) {
		PageTable_t* Table = MmVirtualCreatePageTable();
        AtomicPointer      = &PageDirectory->pTables[i];

		// Install the table into the given page-directory
		atomic_store_explicit(AtomicPointer, (PhysicalAddress_t)Table | Flags, memory_order_relaxed);
		PageDirectory->vTables[i] = (uintptr_t)Table;
	}
}

/* MmVirtualGetMasterTable
 * Helper function to retrieve the current active directory. */
PageDirectory_t*
MmVirtualGetMasterTable(
    _In_  SystemMemorySpace_t* MemorySpace,
    _In_  VirtualAddress_t     Address,
    _Out_ PageDirectory_t**    ParentDirectory,
    _Out_ int*                 IsCurrent)
{
    PageDirectory_t* Directory = (PageDirectory_t*)MemorySpace->Data[MEMORY_SPACE_DIRECTORY];
    PageDirectory_t* Parent    = NULL;

	assert(Directory != NULL);

    // If there is no parent then we ignore it as we don't have to synchronize with kernel directory.
    // We always have the shared page-tables mapped. The address must be below the thread-specific space
    if (MemorySpace->ParentHandle != UUID_INVALID) {
        if (Address < MEMORY_LOCATION_RING3_THREAD_START) {
            SystemMemorySpace_t* MemorySpaceParent = (SystemMemorySpace_t*)LookupHandle(MemorySpace->ParentHandle);
            Parent = (PageDirectory_t*)MemorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
        }
    }
    *IsCurrent       = (MemorySpace == GetCurrentMemorySpace()) ? 1 : 0;
    *ParentDirectory = Parent;
    return Directory;
}

/* MmVirtualGetTable
 * Helper function to retrieve a table from the given directory. */
PageTable_t*
MmVirtualGetTable(
    _In_ PageDirectory_t* ParentPageDirectory,
    _In_ PageDirectory_t* PageDirectory,
    _In_ uintptr_t        Address,
    _In_ int              IsCurrent,
    _In_ int              CreateIfMissing,
    _In_ Flags_t          CreateFlags,
    _Out_ int*            Update)
{
    atomic_uint* AtomicPointerParent;
    atomic_uint* AtomicPointer;
    PageTable_t* Table          = NULL;
    int          PageTableIndex = PAGE_DIRECTORY_INDEX(Address);
    uint32_t     ParentMapping;

    // Load the entry from the table
    AtomicPointer = &PageDirectory->pTables[PageTableIndex];
    ParentMapping = atomic_load(AtomicPointer);
    *Update       = 0; // Not used on x32, only 64

    // Sanitize PRESENT status
	if (ParentMapping & PAGE_PRESENT) {
        Table = (PageTable_t*)PageDirectory->vTables[PageTableIndex];
	    assert(Table != NULL);
	}
    else {
        // Table not present, before attemping to create, sanitize parent
SyncWithParent:
        ParentMapping = 0;
        if (ParentPageDirectory != NULL) {
            AtomicPointerParent = &ParentPageDirectory->pTables[PageTableIndex];
            ParentMapping       = atomic_load(AtomicPointerParent);
        }

        // Check the parent-mapping
        if (ParentMapping & PAGE_PRESENT) {
            // Update our page-directory and reload
            atomic_store(AtomicPointer, ParentMapping | PAGETABLE_INHERITED);
            PageDirectory->vTables[PageTableIndex]  = ParentPageDirectory->vTables[PageTableIndex];
            Table                                   = (PageTable_t*)PageDirectory->vTables[PageTableIndex];
            assert(Table != NULL);
        }
        else if (CreateIfMissing) {
            // Allocate, do a CAS and see if it works, if it fails retry our operation
            uintptr_t TablePhysical;
            Table = (PageTable_t*)kmalloc_ap(PAGE_SIZE, &TablePhysical);
		    assert(Table != NULL);
            memset((void*)Table, 0, sizeof(PageTable_t));

            // Now perform the synchronization
            TablePhysical |= CreateFlags;
            if (ParentPageDirectory != NULL && !atomic_compare_exchange_strong(
                AtomicPointerParent, &ParentMapping, TablePhysical)) {
                // Start over as someone else beat us to the punch
                kfree((void*)Table);
                goto SyncWithParent;
            }

            // Update us and mark our copy as INHERITED
            TablePhysical |= PAGETABLE_INHERITED;
            atomic_store(AtomicPointer, TablePhysical);
            PageDirectory->vTables[PageTableIndex] = (uintptr_t)Table;
        }

		// Reload CR3 directory to force the MMIO to see our changes 
		if (IsCurrent) {
			memory_reload_cr3();
		}
    }
    return Table;
}

/* CloneVirtualSpace
 * Clones a new virtual memory space for an application to use. */
OsStatus_t
CloneVirtualSpace(
    _In_ SystemMemorySpace_t*   MemorySpaceParent, 
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ int                    Inherit)
{
    atomic_uint*     AtomicPointer;
    PageDirectory_t* SystemDirectory = (PageDirectory_t*)GetDomainMemorySpace()->Data[MEMORY_SPACE_DIRECTORY];
    PageDirectory_t* ParentDirectory = NULL;
    PageDirectory_t* PageDirectory;
    uintptr_t        PhysicalAddress;
    int i;

    // Lookup which table-region is the stack region
    int ThreadRegion    = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int ThreadRegionEnd = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_END);

    PageDirectory = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &PhysicalAddress);
    memset(PageDirectory, 0, sizeof(PageDirectory_t));

    // Determine parent
    if (MemorySpaceParent != NULL) {
        ParentDirectory = (PageDirectory_t*)MemorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
    }

    // Initialize base mappings
    for (i = 0; i < ENTRIES_PER_PAGE; i++) {
        uint32_t KernelMapping, CurrentMapping;

        // Sanitize stack region, never copy
        if (i >= ThreadRegion && i <= ThreadRegionEnd) {
            continue;
        }

        // Sanitize if it's inside kernel region
        if (SystemDirectory->vTables[i] != 0) {
            // Update the physical table
            AtomicPointer = &SystemDirectory->pTables[i];
            KernelMapping = atomic_load(AtomicPointer);
            
            AtomicPointer = &PageDirectory->pTables[i];
            atomic_store(AtomicPointer, KernelMapping);

            // Copy virtual
            PageDirectory->vTables[i] = SystemDirectory->vTables[i];
            continue;
        }

        // Inherit? We must mark that table inherited to avoid
        // it being freed again
        if (Inherit && ParentDirectory != NULL) {
            AtomicPointer  = &ParentDirectory->pTables[i];
            CurrentMapping = atomic_load(AtomicPointer);
            if (CurrentMapping & PAGE_PRESENT) {
                AtomicPointer = &PageDirectory->pTables[i];
                atomic_store(AtomicPointer, CurrentMapping | PAGETABLE_INHERITED);
                PageDirectory->vTables[i] = ParentDirectory->vTables[i];
            }
        }
    }

    // Update the configuration data for the memory space
	MemorySpace->Data[MEMORY_SPACE_CR3]       = PhysicalAddress;
	MemorySpace->Data[MEMORY_SPACE_DIRECTORY] = (uintptr_t)PageDirectory;

    // Create new resources for the happy new parent :-)
    if (MemorySpaceParent == NULL) {
        MemorySpace->Data[MEMORY_SPACE_IOMAP] = (uintptr_t)kmalloc(GDT_IOMAP_SIZE);
        if (MemorySpace->Flags & MEMORY_SPACE_APPLICATION) {
            memset((void*)MemorySpace->Data[MEMORY_SPACE_IOMAP], 0xFF, GDT_IOMAP_SIZE);
        }
        else {
            memset((void*)MemorySpace->Data[MEMORY_SPACE_IOMAP], 0, GDT_IOMAP_SIZE);
        }
    }
    return OsSuccess;
}

/* DestroyVirtualSpace
 * Destroys and cleans up any resources used by the virtual address space. */
OsStatus_t
DestroyVirtualSpace(
    _In_ SystemMemorySpace_t* SystemMemorySpace)
{
    atomic_uint*     AtomicPointer;
    PageDirectory_t* Pd = (PageDirectory_t*)SystemMemorySpace->Data[MEMORY_SPACE_DIRECTORY];
    int              i, j;

    // Iterate page-mappings
    for (i = 0; i < ENTRIES_PER_PAGE; i++) {
        PageTable_t *Table;
        uint32_t CurrentMapping;

        // Do some initial checks on the virtual member to avoid atomics
        // If it's empty or if it's a kernel page table ignore it
        if (Pd->vTables[i] == 0) {
            continue;
        }

        // Load the mapping, then perform checks for inheritation or a system
        // mapping which is done by kernel page-directory
        AtomicPointer  = &Pd->pTables[i];
        CurrentMapping = atomic_load_explicit(AtomicPointer, memory_order_relaxed);
        if (CurrentMapping & (PAGE_SYSTEM_MAP | PAGETABLE_INHERITED)) {
            continue;
        }

        // Iterate pages in table
        Table = (PageTable_t*)Pd->vTables[i];
        for (j = 0; j < ENTRIES_PER_PAGE; j++) {
            AtomicPointer  = &Table->Pages[j];
            CurrentMapping = atomic_load_explicit(AtomicPointer, memory_order_relaxed);
            if ((CurrentMapping & (PAGE_SYSTEM_MAP | PAGE_PERSISTENT)) || 
                !(CurrentMapping & PAGE_PRESENT)) {
                continue;
            }

            // If it has a mapping - free it
            if ((CurrentMapping & PAGE_MASK) != 0) {
                if (FreeSystemMemory(CurrentMapping & PAGE_MASK, PAGE_SIZE) != OsSuccess) {
                    ERROR("Tried to free page %i (0x%x), but was not allocated", j, CurrentMapping);
                }
            }
        }
        kfree(Table);
    }
    kfree(Pd);

    // Free the resources allocated specifically for this
    if (SystemMemorySpace->ParentHandle == UUID_INVALID) {
        kfree((void*)SystemMemorySpace->Data[MEMORY_SPACE_IOMAP]);
    }
    return OsSuccess;
}

/* InitializeVirtualSpace
 * Initializes the virtual memory space for the kernel. This creates a new kernel page-directory
 * or reuses the existing one if it's not the primary core that creates it. */
OsStatus_t
InitializeVirtualSpace(
    _In_ SystemMemorySpace_t*   SystemMemorySpace)
{
	// Variables
    PageDirectory_t *iDirectory;
	PageTable_t *iTable;
    Flags_t KernelPageFlags = 0;
    uintptr_t iPhysical;

	// Trace information
	TRACE("InitializeVirtualSpace()");

    // Can we use global pages for kernel table?
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
        KernelPageFlags |= PAGE_GLOBAL;
    }
    KernelPageFlags |= PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP;

    // Is this the primary core that is getting initialized? Then
    // we should create the core mappings first instead of reusing
    if (GetCurrentProcessorCore() == &GetMachine()->Processor.PrimaryCore) {
        size_t BytesToMap               = 0;
        PhysicalAddress_t PhysicalBase  = 0;
        VirtualAddress_t VirtualBase    = 0;

        // Allocate 2 pages for the kernel page directory
        // and reset it by zeroing it out
        iDirectory = (PageDirectory_t*)AllocateSystemMemory(
            sizeof(PageDirectory_t), MEMORY_ALLOCATION_MASK, 0);
        memset((void*)iDirectory, 0, sizeof(PageDirectory_t));
        iPhysical = (uintptr_t)iDirectory;

        // Due to how it works with multiple cpu's, we need to make sure all shared
        // tables already are mapped in the upper-most level of the page-directory
        TRACE("Mapping the kernel region from 0x%x => 0x%x", MEMORY_LOCATION_KERNEL, MEMORY_LOCATION_KERNEL_END);
        MmVirtualMapMemoryRange(iDirectory, 0, MEMORY_LOCATION_KERNEL_END, KernelPageFlags);

        // Identity map some of the regions
        // Kernel image region
        // Kernel video region
        MmVirtualFillPageTable((PageTable_t*)iDirectory->vTables[0], 0x1000, 0x1000, KernelPageFlags);
        BytesToMap      = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;
        PhysicalBase    = VideoGetTerminal()->FrameBufferAddress;
        VirtualBase     = MEMORY_LOCATION_VIDEO;
        while (BytesToMap) {
            iTable          = (PageTable_t*)iDirectory->vTables[PAGE_DIRECTORY_INDEX(VirtualBase)];
            MmVirtualFillPageTable(iTable, PhysicalBase, VirtualBase, KernelPageFlags | PAGE_USER); // @todo is PAGE_USER neccessary?
            BytesToMap      -= MIN(BytesToMap, TABLE_SPACE_SIZE);
            PhysicalBase    += TABLE_SPACE_SIZE;
            VirtualBase     += TABLE_SPACE_SIZE;
        }

        // Update video address to the new
        VideoGetTerminal()->FrameBufferAddress = MEMORY_LOCATION_VIDEO;

        // Update the configuration data for the memory space
        SystemMemorySpace->Data[MEMORY_SPACE_CR3]       = iPhysical;
        SystemMemorySpace->Data[MEMORY_SPACE_DIRECTORY] = (uintptr_t)iDirectory;
        SystemMemorySpace->Data[MEMORY_SPACE_IOMAP]     = TssGetBootIoSpace();

        // Update and switch page-directory for the calling core
        SwitchVirtualSpace(SystemMemorySpace);
        memory_set_paging(1);
    }
    else {
        // Create a new page directory but copy all kernel mappings to the domain specific memory
        //iDirectory = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &iPhysical);
        NOTIMPLEMENTED("Implement initialization of other-domain virtaul spaces");
    }
    return OsSuccess;
}
