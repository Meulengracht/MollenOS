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
#include <system/addressspace.h>
#include <system/video.h>
#include <system/utils.h>
#include <threading.h>
#include <memory.h>
#include <debug.h>
#include <heap.h>
#include <arch.h>
#include <apic.h>
#include <cpu.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Globals 
 * Needed for the virtual memory manager to keep
 * track of current directories */
static PageDirectory_t *KernelMasterTable       = NULL;
static _Atomic(uintptr_t) ReservedMemoryPointer = ATOMIC_VAR_INIT(0);
static MemorySynchronizationObject_t SyncData   = { SPINLOCK_INIT, 0 };

/* Extern assembly functions that are
 * implemented in _paging.asm */
extern void memory_set_paging(int enable);
extern void memory_load_cr3(uintptr_t pda);
extern void memory_reload_cr3(void);
extern void memory_invalidate_addr(uintptr_t pda);
extern uint32_t memory_get_cr3(void);

/* PageSynchronizationHandler
 * Synchronizes the page address specified in the MemorySynchronization Object. */
InterruptStatus_t
PageSynchronizationHandler(
    _In_ void*              Context)
{
    // Variables
    AddressSpace_t *Current = AddressSpaceGetCurrent();

    // Make sure the current address space is matching
    if (Current->Parent == (AddressSpace_t*)SyncData.ParentPagingData || 
        Current         == (AddressSpace_t*)SyncData.ParentPagingData) {
        memory_invalidate_addr(SyncData.Address);
    }
    SyncData.CallsCompleted++;
    return InterruptHandled;
}

/* MmVirtualSynchronizePage
 * Synchronizes the page address across cores to make sure they have the
 * latest revision of the page-table cached. */
void
MmVirtualSynchronizePage(
    _In_ PageDirectory_t*   ParentDirectory,
    _In_ uintptr_t          Address)
{
    // Multiple cores?
    if (GetMachine()->NumberOfCores == 1) {
        return;
    }
    assert(InterruptGetActiveStatus() == 0);
    AtomicSectionEnter(&SyncData.SyncObject);
    
    // Setup arguments
    SyncData.ParentPagingData   = ParentDirectory;
    SyncData.Address            = Address;
    SyncData.CallsCompleted     = 0;

    // Synchronize the page-tables
    ApicSendInterrupt(InterruptAllButSelf, UUID_INVALID, INTERRUPT_SYNCHRONIZE_PAGE);
    
    // Wait for all cpu's to have handled this.
    while(SyncData.CallsCompleted != (GetMachine()->NumberOfCores - 1));
    AtomicSectionLeave(&SyncData.SyncObject);
}

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
        atomic_store(&pTable->Pages[PAGE_TABLE_INDEX(vAddress)], pAddress | Flags);
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
	// Variables
	unsigned i, k;

	// Iterate the afflicted page-tables
	for (i = PAGE_DIRECTORY_INDEX(AddressStart), k = 0;
		i < (PAGE_DIRECTORY_INDEX(AddressStart + Length - 1) + 1);
		i++, k++) {
		PageTable_t *Table = MmVirtualCreatePageTable();

		// Install the table into the given page-directory
		atomic_store(&PageDirectory->pTables[i], (PhysicalAddress_t)Table | Flags);
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
	GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR] = (uintptr_t)PageDirectory;
	memory_load_cr3(Pdb);
	return OsSuccess;
}

/* InitializeMemoryForApplicationCore
 * Initializes the missing memory setup for the calling cpu */
void
InitializeMemoryForApplicationCore(void)
{
    // Set current page-directory to kernel
    memory_load_cr3((uintptr_t)KernelMasterTable);
    memory_set_paging(1);

    // Set active now that we have memory mappings
    GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR] = (uintptr_t)KernelMasterTable;
}

/* MmVirtualGetDirectory
 * Helper function to retrieve the current active directory. */
PageDirectory_t*
MmVirtualGetDirectory(
    _In_  void*             PageDirectory,
    _Out_ int*              IsCurrent)
{
    // Variables
    PageDirectory_t *Directory = (PageDirectory_t*)PageDirectory;

	// Determine page directory 
	// If we were given null, select the current
	if (Directory == NULL) {
		Directory = (PageDirectory_t*)GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR];
        *IsCurrent = 1;
	}
	else if ((PageDirectory_t*)GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR] == Directory) {
		*IsCurrent = 1;
	}
	assert(Directory != NULL);
    return Directory;
}

/* MmVirtualGetTable
 * Helper function to retrieve a table from the given directory. */
PageTable_t*
MmVirtualGetTable(
    _In_ PageDirectory_t*   ParentPageDirectory,
    _In_ PageDirectory_t*   PageDirectory,
    _In_ uintptr_t          Address,
    _In_ int                IsCurrent,
    _In_ int                CreateIfMissing,
    _In_ Flags_t            CreateFlags)
{
    // Variables
    PageTable_t *Table  = NULL;
    int PageTableIndex  = PAGE_DIRECTORY_INDEX(Address);
    uint32_t ParentMapping;

    // Load the entry from the table
    ParentMapping = atomic_load(&PageDirectory->pTables[PageTableIndex]);

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
            ParentMapping = atomic_load(&ParentPageDirectory->pTables[PageTableIndex]);
        }

        // Check the parent-mapping
        if (ParentMapping & PAGE_PRESENT) {
            // Update our page-directory and reload
            atomic_store(&PageDirectory->pTables[PageTableIndex], ParentMapping | PAGE_INHERITED);
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
            TablePhysical |= PAGE_PRESENT | PAGE_WRITE | CreateFlags;
            if (ParentPageDirectory != NULL && !atomic_compare_exchange_strong(
                &ParentPageDirectory->pTables[PageTableIndex], &ParentMapping, TablePhysical)) {
                // Start over as someone else beat us to the punch
                kfree((void*)Table);
                goto SyncWithParent;
            }

            // Update us and mark our copy as INHERITED
            TablePhysical |= PAGE_INHERITED;
            atomic_store(&PageDirectory->pTables[PageTableIndex], TablePhysical);
            PageDirectory->vTables[PageTableIndex] = (uintptr_t)Table;
        }

		// Reload CR3 directory to force the MMIO to see our changes 
		if (IsCurrent) {
			memory_reload_cr3();
		}
    }
    return Table;
}

/* MmVirtualSetFlags
 * Changes memory protection flags for the given virtual address */
OsStatus_t
MmVirtualSetFlags(
    _In_ void*              ParentPageDirectory,
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   vAddress, 
	_In_ Flags_t            Flags)
{
	// Variabes
	int IsCurrent                       = 0;
    PageDirectory_t *ParentDirectory    = (PageDirectory_t*)ParentPageDirectory;
	PageDirectory_t *Directory          = MmVirtualGetDirectory(PageDirectory, &IsCurrent);
	PageTable_t *Table                  = MmVirtualGetTable(ParentDirectory, Directory, vAddress, IsCurrent, 0, 0);
    uint32_t Mapping;

	// Does page table exist?
    if (Table == NULL) {
        return OsError;
    }
    
	// Map it, make sure we mask the page address so we don't accidently set any flags
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(vAddress)]);
    if (!(Mapping & PAGE_SYSTEM_MAP)) {
        atomic_store(&Table->Pages[PAGE_TABLE_INDEX(vAddress)], (Mapping & PAGE_MASK) | (Flags & ATTRIBUTE_MASK));

        // Synchronize with cpus
        MmVirtualSynchronizePage(ParentDirectory, vAddress);
        if (IsCurrent) {
            memory_invalidate_addr(vAddress);
        }
	    return OsSuccess;
    }
    return OsError;
}

/* MmVirtualGetFlags
 * Retrieves memory protection flags for the given virtual address */
OsStatus_t
MmVirtualGetFlags(
    _In_  void*             ParentPageDirectory,
	_In_  void*             PageDirectory, 
	_In_  VirtualAddress_t  vAddress, 
	_Out_ Flags_t*          Flags)
{
	// Variabes
	int IsCurrent                       = 0;
    PageDirectory_t *ParentDirectory    = (PageDirectory_t*)ParentPageDirectory;
	PageDirectory_t *Directory          = MmVirtualGetDirectory(PageDirectory, &IsCurrent);
	PageTable_t *Table                  = MmVirtualGetTable(ParentDirectory, Directory, vAddress, IsCurrent, 0, 0);

	// Does page table exist?
    if (Table == NULL) {
        return OsError;
    }

	// Map it, make sure we mask the page address so we don't accidently set any flags
    if (Flags != NULL) {
        *Flags = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(vAddress)]) & ATTRIBUTE_MASK;
    }
	return OsSuccess;
}

/* MmVirtualMap
 * Installs a new page-mapping in the given page-directory. The type of mapping 
 * is controlled by the Flags parameter. */
OsStatus_t
MmVirtualMap(
    _In_ void*              ParentPageDirectory,
	_In_ void*              PageDirectory, 
	_In_ PhysicalAddress_t  pAddress, 
	_In_ VirtualAddress_t   vAddress, 
	_In_ Flags_t            Flags)
{
	// Variabes
	int IsCurrent                       = 0;
    PageDirectory_t *ParentDirectory    = (PageDirectory_t*)ParentPageDirectory;
	PageDirectory_t *Directory          = NULL;
	PageTable_t *Table                  = NULL;
    OsStatus_t Status                   = OsSuccess;
    uint32_t Mapping;

    // For kernel mappings we would like to mark the mappings global
    if (vAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            Flags |= PAGE_GLOBAL;
        }  
    }

    // Get correct directory and table
    Directory   = MmVirtualGetDirectory(PageDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentDirectory, Directory, vAddress, IsCurrent, 1, Flags);
    assert(Table != NULL);

    // Make sure value is not mapped already, NEVER overwrite a mapping
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(vAddress)]);
SyncTable:
    if (Mapping != 0) {
        if (Flags & PAGE_VIRTUAL) {
            if (Mapping != (pAddress & PAGE_MASK)) {
                FATAL(FATAL_SCOPE_KERNEL, 
                    "Tried to remap fixed virtual address 0x%x => 0x%x (Existing 0x%x)", 
                    vAddress, pAddress, Mapping);
            }
        }
        Status = OsError;
        goto LeaveFunction;
    }

    // Perform the mapping in a weak context, fast operation
    if (!atomic_compare_exchange_weak(&Table->Pages[PAGE_TABLE_INDEX(vAddress)], 
        &Mapping, (pAddress & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | Flags)) {
        goto SyncTable;
    }

	// Last step is to invalidate the address in the MMIO
LeaveFunction:
	if (IsCurrent) {
		memory_invalidate_addr(vAddress);
	}
	return OsSuccess;
}

/* MmVirtualUnmap
 * Unmaps a previous mapping from the given page-directory
 * the mapping must be present */
OsStatus_t
MmVirtualUnmap(
    _In_ void*              ParentPageDirectory,
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   Address)
{
	// Variabes
	int IsCurrent                       = 0;
    PageDirectory_t *ParentDirectory    = (PageDirectory_t*)ParentPageDirectory;
	PageDirectory_t *Directory          = NULL;
	PageTable_t *Table                  = NULL;
    uint32_t Mapping;

    // Get correct directory and table
    Directory   = MmVirtualGetDirectory(PageDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, 0);

    // Did the page-table exist?
    if (Table == NULL) {
        return OsError;
    }

    // Load the mapping
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(Address)]);
SyncTable:
    if (Mapping & PAGE_PRESENT) {
        if (!(Mapping & PAGE_SYSTEM_MAP)) {
            // Present, not system map
            // Perform the un-mapping in a weak context, fast operation
            if (!atomic_compare_exchange_weak(&Table->Pages[PAGE_TABLE_INDEX(Address)], 
                &Mapping, 0)) {
                goto SyncTable;
            }

            // Release memory, but don't if it is a virtual mapping, that means we 
            // should not free the physical page
            if (!(Mapping & PAGE_VIRTUAL)) {
                MmPhysicalFreeBlock(Mapping & PAGE_MASK);
            }

            // Last step is to validate the page-mapping
            // now this should be an IPC to all cpu's
            MmVirtualSynchronizePage(ParentDirectory, Address);
            if (IsCurrent) {
                memory_invalidate_addr(Address);
            }
            return OsSuccess;
        }
    }
    return OsError;
}

/* MmVirtualGetMapping
 * Retrieves the physical address mapping of the
 * virtual memory address given - from the page directory that is given */
PhysicalAddress_t
MmVirtualGetMapping(
    _In_ void*              ParentPageDirectory,
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   Address)
{
	// Variabes
	int IsCurrent                       = 0;
    PageDirectory_t *ParentDirectory    = (PageDirectory_t*)ParentPageDirectory;
	PageDirectory_t *Directory          = NULL;
	PageTable_t *Table                  = NULL;
	uint32_t Mapping;

    // Get correct directory and table
    Directory   = MmVirtualGetDirectory(PageDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, 0);

	// Does page table exist?
    if (Table == NULL) {
        return 0;
    }

    // Get the address and return with proper offset
	Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(Address)]);

    // Make sure we still return 0 if the mapping is indeed 0
    if ((Mapping & PAGE_MASK) == 0 || !(Mapping & PAGE_PRESENT)) {
        return 0;
    }
	return ((Mapping & PAGE_MASK) + (Address & ATTRIBUTE_MASK));
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
    PageDirectory_t *CurrPd     = (PageDirectory_t*)GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR];
    PageDirectory_t *KernPd     = KernelMasterTable;
    int i;

    // Copy at max kernel directories up to MEMORY_SEGMENT_RING3_BASE
    int KernelRegion            = 0;
    int KernelRegionEnd         = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_KERNEL_END);

    // Lookup which table-region is the stack region
    int ThreadRegion            = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int ThreadRegionEnd         = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_END);
	
    memset(NewPd, 0, sizeof(PageDirectory_t));

    // Initialize base mappings
    for (i = 0; i < ENTRIES_PER_PAGE; i++) {
        uint32_t KernelMapping, CurrentMapping;

        // Sanitize stack region, never copy
        if (i >= ThreadRegion && i <= ThreadRegionEnd) {
            continue;
        }

        // Sanitize if it's inside kernel region
        if (i >= KernelRegion && i < KernelRegionEnd) {
            // Update the physical table
            KernelMapping = atomic_load(&KernPd->pTables[i]);
            atomic_store(&NewPd->pTables[i], KernelMapping);

            // Copy virtual
            NewPd->vTables[i] = KernPd->vTables[i];
            continue;
        }

        // Inherit? We must mark that table inherited to avoid
        // it being freed again
        if (Inherit) {
            CurrentMapping = atomic_load(&CurrPd->pTables[i]);
            if (CurrentMapping & PAGE_PRESENT) {
                atomic_store(&NewPd->pTables[i], CurrentMapping | PAGE_INHERITED);
                NewPd->vTables[i] = CurrPd->vTables[i];
            }
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
        PageTable_t *Table;
        uint32_t CurrentMapping;

        // Do some initial checks on the virtual member to avoid atomics
        // If it's empty or if it's a kernel page table ignore it
        if (Pd->vTables[i] == 0 || Pd->vTables[i] == KernPd->vTables[i]) {
            continue;
        }

        // The rest of the checks we must load for
        // Like skip our inherited tables
        CurrentMapping = atomic_load(&Pd->pTables[i]);
        if (CurrentMapping & PAGE_INHERITED) {
            continue;
        }
        Table = (PageTable_t*)Pd->vTables[i];

        // Iterate pages in table
        for (j = 0; j < ENTRIES_PER_PAGE; j++) {
            CurrentMapping = atomic_load(&Table->Pages[j]);
            if (CurrentMapping & PAGE_VIRTUAL) {
                continue;
            }

            // If it has a mapping - free it
            if ((CurrentMapping & PAGE_MASK) != 0) {
                if (MmPhysicalFreeBlock(CurrentMapping & PAGE_MASK) != OsSuccess) {
                    ERROR("Tried to free page %i (0x%x) , but was not allocated", j, CurrentMapping);
                }
            }
        }

        // Free the page-table
        kfree(Table);
    }
    
    // Free the page-directory
    kfree(Pd);
    return OsSuccess;
}

/* MmReserveMemory
 * Reserves memory for system use - should be allocated
 * from a fixed memory region that won't interfere with
 * general usage */
VirtualAddress_t*
MmReserveMemory(
	_In_ int Pages)
{
	return (VirtualAddress_t*)atomic_fetch_add(&ReservedMemoryPointer, (PAGE_SIZE * Pages));
}

/* MmVirtualInit
 * Initializes the virtual memory system and
 * installs default kernel mappings */
OsStatus_t
MmVirtualInit(void)
{
	// Variables
	AddressSpace_t KernelSpace;
	PageTable_t *iTable             = NULL;
    size_t BytesToMap               = 0;
    PhysicalAddress_t PhysicalBase  = 0;
    VirtualAddress_t VirtualBase    = 0;
    Flags_t KernelPageFlags         = 0;

	// Trace information
	TRACE("MmVirtualInit()");

	// Initialize reserved pointer
	atomic_store(&ReservedMemoryPointer, MEMORY_LOCATION_RESERVED);

    // Can we use global pages for kernel table?
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
        KernelPageFlags |= PAGE_GLOBAL;
    }
    KernelPageFlags |= PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP;

	// Allocate 2 pages for the kernel page directory
	// and reset it by zeroing it out
	KernelMasterTable = (PageDirectory_t*)MmPhysicalAllocateBlock(MEMORY_ALLOCATION_MASK, 2);
	memset((void*)KernelMasterTable, 0, sizeof(PageDirectory_t));
    memset((void*)&KernelSpace, 0, sizeof(AddressSpace_t));

	// Due to how it works with multiple cpu's, we need to make sure all shared
    // tables already are mapped in the upper-most level of the page-directory
    TRACE("Mapping the kernel region from 0x%x => 0x%x", MEMORY_LOCATION_KERNEL, MEMORY_LOCATION_KERNEL_END);
	MmVirtualMapMemoryRange(KernelMasterTable, 0, MEMORY_LOCATION_KERNEL_END, KernelPageFlags);

    // Identity map some of the regions
    // Kernel image region
    // Kernel video region
	MmVirtualFillPageTable((PageTable_t*)KernelMasterTable->vTables[0], 0x1000, 0x1000, KernelPageFlags);
    BytesToMap      = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;
    PhysicalBase    = VideoGetTerminal()->FrameBufferAddress;
    VirtualBase     = MEMORY_LOCATION_VIDEO;
    while (BytesToMap) {
        iTable          = (PageTable_t*)KernelMasterTable->vTables[PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_VIDEO)];
        MmVirtualFillPageTable(iTable, PhysicalBase, VirtualBase, KernelPageFlags | PAGE_USER); // @todo is PAGE_USER neccessary?
        BytesToMap      -= MIN(BytesToMap, TABLE_SPACE_SIZE);
        PhysicalBase    += TABLE_SPACE_SIZE;
        VirtualBase     += TABLE_SPACE_SIZE;
    }

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
