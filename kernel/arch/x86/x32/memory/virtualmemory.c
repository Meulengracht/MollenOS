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
#include <system/video.h>
#include <system/utils.h>
#include <threading.h>
#include <machine.h>
#include <memory.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <arch.h>
#include <cpu.h>

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
	// Variables
	uintptr_t pAddress, vAddress;
	int i;

	// Iterate through pages and map them
	for (i = PAGE_TABLE_INDEX(vAddressStart), pAddress = pAddressStart, vAddress = vAddressStart;
		i < ENTRIES_PER_PAGE; i++, pAddress += PAGE_SIZE, vAddress += PAGE_SIZE) {
        atomic_store_explicit(&pTable->Pages[PAGE_TABLE_INDEX(vAddress)], pAddress | Flags, memory_order_relaxed);
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
		atomic_store_explicit(&PageDirectory->pTables[i], (PhysicalAddress_t)Table | Flags, memory_order_relaxed);
		PageDirectory->vTables[i] = (uintptr_t)Table;
	}
}

/* SwitchVirtualSpace
 * Updates the currently active memory space for the calling core. */
OsStatus_t
SwitchVirtualSpace(
    SystemMemorySpace_t*        SystemMemorySpace)
{
    // Variables
    assert(SystemMemorySpace != NULL);
    assert(SystemMemorySpace->Data[MEMORY_SPACE_CR3] != 0);
    assert(SystemMemorySpace->Data[MEMORY_SPACE_DIRECTORY] != 0);

    // Update current page-directory
	memory_load_cr3(SystemMemorySpace->Data[MEMORY_SPACE_CR3]);
	return OsSuccess;
}

/* MmVirtualGetDirectory
 * Helper function to retrieve the current active directory. */
PageDirectory_t*
MmVirtualGetDirectory(
    _In_  SystemMemorySpace_t*  MemorySpace,
    _In_  VirtualAddress_t      Address,
    _Out_ PageDirectory_t**     ParentDirectory,
    _Out_ int*                  IsCurrent)
{
    // Variables
    PageDirectory_t *Directory  = (PageDirectory_t*)MemorySpace->Data[MEMORY_SPACE_DIRECTORY];
    PageDirectory_t *Parent     = NULL;
    PageDirectory_t *Current    = (PageDirectory_t*)GetCurrentSystemMemorySpace()->Data[MEMORY_SPACE_DIRECTORY];

	assert(Directory != NULL);
    assert(Current != NULL);

    // If there is no parent then we ignore it as we don't have to synchronize with kernel directory.
    // We always have the shared page-tables mapped. The address must be below the thread-specific space
    if (MemorySpace->Parent != NULL) {
        if (Address < MEMORY_LOCATION_RING3_THREAD_START) {
            Parent = (PageDirectory_t*)MemorySpace->Parent->Data[MEMORY_SPACE_DIRECTORY];
        }
    }

    // Update the provided pointers
    *IsCurrent          = (Directory == Current) ? 1 : 0;
    *ParentDirectory    = Parent;
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

/* SetVirtualPageAttributes
 * Changes memory protection flags for the given virtual address */
OsStatus_t
SetVirtualPageAttributes(
	_In_ SystemMemorySpace_t*   MemorySpace,
	_In_ VirtualAddress_t       Address,
    _In_ Flags_t                Flags)
{
	// Variabes
    PageDirectory_t *ParentDirectory;
	PageDirectory_t *Directory;
	PageTable_t *Table;
    uint32_t Mapping;
    Flags_t ConvertedFlags;
	int IsCurrent;

    Directory       = MmVirtualGetDirectory(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table           = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, 0);
    ConvertedFlags  = ConvertSystemSpaceToPaging(Flags);

	// Does page table exist?
    if (Table == NULL) {
        return OsError;
    }
 
	// Map it, make sure we mask the page address so we don't accidently set any flags
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(Address)]);
    if (!(Mapping & PAGE_SYSTEM_MAP)) {
        atomic_store(&Table->Pages[PAGE_TABLE_INDEX(Address)], (Mapping & PAGE_MASK) | ConvertedFlags);

        // Synchronize with cpus
        SynchronizeVirtualPage(MemorySpace, Address);
        if (IsCurrent) {
            memory_invalidate_addr(Address);
        }
	    return OsSuccess;
    }
    return OsError;
}

/* GetVirtualPageAttributes
 * Retrieves memory protection flags for the given virtual address */
OsStatus_t
GetVirtualPageAttributes(
	_In_  SystemMemorySpace_t*  MemorySpace,
	_In_  VirtualAddress_t      Address,
	_Out_ Flags_t*              Flags)
{
	// Variabes
    PageDirectory_t *ParentDirectory;
	PageDirectory_t *Directory;
	PageTable_t *Table;
	int IsCurrent;
    Flags_t OriginalFlags;

    Directory   = MmVirtualGetDirectory(MemorySpace, Address, &ParentDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentDirectory, Directory, Address, IsCurrent, 0, 0);

	// Does page table exist?
    if (Table == NULL) {
        return OsError;
    }

	// Map it, make sure we mask the page address so we don't accidently set any flags
    if (Flags != NULL) {
        OriginalFlags   = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(Address)]) & ATTRIBUTE_MASK;
        *Flags          = ConvertPagingToSystemSpace(OriginalFlags);
    }
	return OsSuccess;
}

/* SetVirtualPageMapping
 * Installs a new page-mapping in the given page-directory. The type of mapping 
 * is controlled by the Flags parameter. */
OsStatus_t
SetVirtualPageMapping(
	_In_ SystemMemorySpace_t*   MemorySpace,
	_In_ PhysicalAddress_t      pAddress,
	_In_ VirtualAddress_t       vAddress,
	_In_ Flags_t                Flags)
{
	// Variabes
    PageDirectory_t *ParentDirectory;
	PageDirectory_t *Directory;
	PageTable_t *Table;
    uint32_t Mapping;
    Flags_t ConvertedFlags;
	int IsCurrent;

    OsStatus_t Status = OsSuccess;

    Directory       = MmVirtualGetDirectory(MemorySpace, (vAddress & PAGE_MASK), &ParentDirectory, &IsCurrent);
    Table           = MmVirtualGetTable(ParentDirectory, Directory, (vAddress & PAGE_MASK), IsCurrent, 1, Flags);
    ConvertedFlags  = ConvertSystemSpaceToPaging(Flags);

    // For kernel mappings we would like to mark the mappings global
    if (vAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            ConvertedFlags |= PAGE_GLOBAL;
        }  
    }

    // If table is null creation failed
    assert(Table != NULL);

    // Make sure value is not mapped already, NEVER overwrite a mapping
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX((vAddress & PAGE_MASK))]);
SyncTable:
    if (Mapping != 0) {
        if (ConvertedFlags & PAGE_VIRTUAL) {
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
    if (!atomic_compare_exchange_weak(&Table->Pages[PAGE_TABLE_INDEX((vAddress & PAGE_MASK))], 
        &Mapping, (pAddress & PAGE_MASK) | ConvertedFlags)) {
        goto SyncTable;
    }

	// Last step is to invalidate the address in the MMIO
LeaveFunction:
	if (IsCurrent) {
		memory_invalidate_addr((vAddress & PAGE_MASK));
	}
	return OsSuccess;
}

/* ClearVirtualPageMapping
 * Unmaps a previous mapping from the given page-directory
 * the mapping must be present */
OsStatus_t
ClearVirtualPageMapping(
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ VirtualAddress_t       Address)
{
	// Variabes
    PageDirectory_t *ParentDirectory;
	PageDirectory_t *Directory;
	PageTable_t *Table;
    uint32_t Mapping;
	int IsCurrent;

    Directory   = MmVirtualGetDirectory(MemorySpace, Address, &ParentDirectory, &IsCurrent);
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
            if (!atomic_compare_exchange_weak(&Table->Pages[PAGE_TABLE_INDEX(Address)], &Mapping, 0)) {
                goto SyncTable;
            }

            // Release memory, but don't if it is a virtual mapping, that means we 
            // should not free the physical page
            if (!(Mapping & PAGE_VIRTUAL)) {
                FreeSystemMemory(Mapping & PAGE_MASK, PAGE_SIZE);
            }

            // Last step is to validate the page-mapping
            // now this should be an IPC to all cpu's
            SynchronizeVirtualPage(MemorySpace, Address);
            if (IsCurrent) {
                memory_invalidate_addr(Address);
            }
            return OsSuccess;
        }
    }
    return OsError;
}

/* GetVirtualPageMapping
 * Retrieves the physical address mapping of the
 * virtual memory address given - from the page directory that is given */
uintptr_t
GetVirtualPageMapping(
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ VirtualAddress_t       Address)
{
	// Variabes
    PageDirectory_t *ParentDirectory;
	PageDirectory_t *Directory;
	PageTable_t *Table;
    uint32_t Mapping;
	int IsCurrent;

    Directory   = MmVirtualGetDirectory(MemorySpace, Address, &ParentDirectory, &IsCurrent);
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

/* CloneVirtualSpace
 * Clones a new virtual memory space for an application to use. */
OsStatus_t
CloneVirtualSpace(
    _In_ SystemMemorySpace_t*   MemorySpaceParent, 
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ int                    Inherit)
{
    // Variables
    PageDirectory_t *SystemDirectory = (PageDirectory_t*)GetSystemMemorySpace()->Data[MEMORY_SPACE_DIRECTORY];
    PageDirectory_t *ParentDirectory = NULL;
    PageDirectory_t *PageDirectory;
    uintptr_t PhysicalAddress;
    int i;

    // Lookup which table-region is the stack region
    int ThreadRegion            = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int ThreadRegionEnd         = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_END);

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
            KernelMapping = atomic_load(&SystemDirectory->pTables[i]);
            atomic_store(&PageDirectory->pTables[i], KernelMapping);

            // Copy virtual
            PageDirectory->vTables[i] = SystemDirectory->vTables[i];
            continue;
        }

        // Inherit? We must mark that table inherited to avoid
        // it being freed again
        if (Inherit && ParentDirectory != NULL) {
            CurrentMapping = atomic_load(&ParentDirectory->pTables[i]);
            if (CurrentMapping & PAGE_PRESENT) {
                atomic_store(&PageDirectory->pTables[i], CurrentMapping | PAGE_INHERITED);
                PageDirectory->vTables[i] = ParentDirectory->vTables[i];
            }
        }
    }

    // Update the configuration data for the memory space
	MemorySpace->Data[MEMORY_SPACE_CR3]       = PhysicalAddress;
	MemorySpace->Data[MEMORY_SPACE_DIRECTORY] = (uintptr_t)PageDirectory;
    return OsSuccess;
}

/* DestroyVirtualSpace
 * Destroys and cleans up any resources used by the virtual address space. */
OsStatus_t
DestroyVirtualSpace(
    _In_ SystemMemorySpace_t*   SystemMemorySpace)
{
    // Variables
    PageDirectory_t *Pd = (PageDirectory_t*)SystemMemorySpace->Data[MEMORY_SPACE_DIRECTORY];
    int i, j;

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
        CurrentMapping = atomic_load_explicit(&Pd->pTables[i], memory_order_relaxed);
        if (CurrentMapping & (PAGE_SYSTEM_MAP | PAGE_INHERITED)) {
            continue;
        }

        // Iterate pages in table
        Table = (PageTable_t*)Pd->vTables[i];
        for (j = 0; j < ENTRIES_PER_PAGE; j++) {
            CurrentMapping = atomic_load_explicit(&Table->Pages[j], memory_order_relaxed);
            if (CurrentMapping & PAGE_VIRTUAL) {
                continue;
            }

            // If it has a mapping - free it
            if ((CurrentMapping & PAGE_MASK) != 0) {
                if (FreeSystemMemory(CurrentMapping & PAGE_MASK, PAGE_SIZE) != OsSuccess) {
                    ERROR("Tried to free page %i (0x%x), but was not allocated", j, CurrentMapping);
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
            sizeof(PageDirectory_t), MEMORY_ALLOCATION_MASK, MEMORY_DOMAIN);
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
            iTable          = (PageTable_t*)iDirectory->vTables[PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_VIDEO)];
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

        // Update and switch page-directory for the calling core
        SwitchVirtualSpace(SystemMemorySpace);
        memory_set_paging(1);
    }
    else {
        // Create a new page directory but copy all kernel mappings to the domain specific memory
        iDirectory = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &iPhysical);
        NOTIMPLEMENTED("Implement initialization of other-domain virtaul spaces");
    }
    return OsSuccess;
}
