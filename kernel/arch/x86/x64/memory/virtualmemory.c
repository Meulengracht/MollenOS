/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * MollenOS x86-64 Virtual Memory Manager
 * - Contains the implementation of virtual memory management
 *   for the X86-64 Architecture 
 */
#define __MODULE		"VMEM"
//#define __TRACE

/* Includes
 * - System */
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
static PageMasterTable_t *KernelMasterTable = NULL;
static PageMasterTable_t *MasterTables[MAX_SUPPORTED_CPUS];
static Spinlock_t GlobalMemoryLock          = SPINLOCK_INIT;
static uintptr_t ReservedMemoryPointer      = 0;

/* Extern acess to system mappings in the
 * physical memory manager */
__EXTERN void memory_load_cr3(uintptr_t pda);
__EXTERN void memory_reload_cr3(void);
__EXTERN void memory_invalidate_addr(uintptr_t pda);
__EXTERN uint64_t memory_get_cr3(void);

// Function helpers for repeating functions where it pays off
// to have them seperate
#define CREATE_STRUCTURE_HELPER(Type, Name) Type* MmVirtualCreate##Name(void) { \
                                            Type *Instance = (Type*)MmPhysicalAllocateBlock(MEMORY_ALLOCATION_MASK, DIVUP(sizeof(Type), PAGE_SIZE)); \
                                            assert(Instance != NULL); \
                                            memset((void*)Instance, 0, sizeof(Type)); \
                                            return Instance; }



/* MmVirtualCreatePageTable
 * Creates and initializes a new empty page-table */
CREATE_STRUCTURE_HELPER(PageTable_t, PageTable)

/* MmVirtualCreatePageDirectory
 * Creates and initializes a new empty page-directory */
CREATE_STRUCTURE_HELPER(PageDirectory_t, PageDirectory)

/* MmVirtualCreatePageDirectoryTable
 * Creates and initializes a new empty page-directory-table */
CREATE_STRUCTURE_HELPER(PageDirectoryTable_t, PageDirectoryTable)

/* MmVirtualFillPageTable
 * Identity maps a memory region inside the given
 * page-table - type of mappings is controlled with Flags */
void 
MmVirtualFillPageTable(
	_In_ PageTable_t*       Table, 
	_In_ PhysicalAddress_t  PhysicalAddress, 
	_In_ VirtualAddress_t   VirtualAddress, 
	_In_ Flags_t            Flags)
{
	// Variables
	uintptr_t PhysicalEntry, VirtualEntry;
	int i;

	// Iterate through pages and map them
	for (i = PAGE_TABLE_INDEX(VirtualAddress), PhysicalEntry = PhysicalAddress, VirtualEntry = VirtualAddress;
		i < ENTRIES_PER_PAGE; i++, PhysicalEntry += PAGE_SIZE, VirtualEntry += PAGE_SIZE) {
		Table->Pages[PAGE_TABLE_INDEX(VirtualEntry)] = PhysicalEntry | Flags;
	}
}

/* MmVirtualIdentityMapMemoryRange
 * Identity maps not only a page-table or a region inside
 * it can identity map an entire memory region and create
 * page-table for the region automatically */
void 
MmVirtualIdentityMapMemoryRange(
	_In_ PageMasterTable_t* PageMaster,
	_In_ PhysicalAddress_t  PhysicalAddressStart, 
	_In_ VirtualAddress_t   VirtualAddressStart, 
	_In_ uintptr_t          Length, 
	_In_ int                Fill, 
	_In_ Flags_t            Flags)
{
	// Variables
    PhysicalAddress_t PhysicalAddress       = PhysicalAddressStart;
    VirtualAddress_t VirtualAddress         = VirtualAddressStart;
    PageDirectoryTable_t *DirectoryTable    = NULL;
    PageDirectory_t *Directory              = NULL;
    PageTable_t *Table                      = NULL;

    // Must be page-table aligned
    assert((PhysicalAddressStart % TABLE_SPACE_SIZE) == 0);
    assert((VirtualAddressStart % TABLE_SPACE_SIZE) == 0);

    // Determine start + end PML4 Index
    int PmIndexStart    = PAGE_LEVEL_4_INDEX(VirtualAddress);
    int PmIndexEnd      = PAGE_LEVEL_4_INDEX(VirtualAddress + Length - 1) + 1;
    for (int PmIndex = PmIndexStart; PmIndex < PmIndexEnd; PmIndex++) {
        // Sanitize existance/mapping
        if (!PageMaster->vTables[PmIndex]) {
            PageMaster->vTables[PmIndex] = (uint64_t)MmVirtualCreatePageDirectoryTable();
            PageMaster->pTables[PmIndex] = PageMaster->vTables[PmIndex] | Flags;
        }
        DirectoryTable = (PageDirectoryTable_t*)PageMaster->vTables[PmIndex];

        // Determine start + end of the page directory pointer index
        int PdpIndexStart   = PAGE_DIRECTORY_POINTER_INDEX(VirtualAddress);
        int PdpIndexEnd     = PAGE_DIRECTORY_POINTER_INDEX(VirtualAddress + Length - 1) + 1;
        for (int PdpIndex = PdpIndexStart; PdpIndex < PdpIndexEnd; PdpIndex++) {
            // Sanitize existance/mapping
            if (!DirectoryTable->vTables[PdpIndex]) {
                DirectoryTable->vTables[PdpIndex] = (uint64_t)MmVirtualCreatePageDirectory();
                DirectoryTable->pTables[PdpIndex] = DirectoryTable->vTables[PdpIndex] | Flags;
            }
            Directory = (PageDirectory_t*)DirectoryTable->vTables[PdpIndex];

            int PdIndexStart    = PAGE_DIRECTORY_INDEX(VirtualAddress);
            int PdIndexEnd      = PAGE_DIRECTORY_INDEX(VirtualAddress + Length - 1) + 1;
            for (int PdIndex = PdIndexStart; PdIndex < PdIndexEnd; PdIndex++) {
                // Sanitize existance/mapping
                if (!Directory->vTables[PdIndex]) {
                    Directory->vTables[PdIndex] = (uint64_t)MmVirtualCreatePageTable();
                    Directory->pTables[PdIndex] = Directory->vTables[PdIndex] | Flags;
                }
                Table = (PageTable_t*)Directory->vTables[PdIndex];

                // Fill with mappings?
                if (Fill) {
                    MmVirtualFillPageTable(Table, PhysicalAddress, VirtualAddress, Flags);
                }
                PhysicalAddress += TABLE_SPACE_SIZE;
                VirtualAddress  += TABLE_SPACE_SIZE;
            }
        }
    }
}

/* MmVirtualSwitchPageDirectory
 * Switches page-directory for the current cpu
 * but the current cpu should be given as parameter
 * as well */
OsStatus_t
MmVirtualSwitchPageDirectory(
	_In_ UUId_t             Cpu, 
	_In_ void*              PageDirectory, 
	_In_ PhysicalAddress_t  Pdb) {
	assert(PageDirectory != NULL);
	MasterTables[Cpu] = (PageMasterTable_t*)PageDirectory;
	memory_load_cr3(Pdb);
	return OsSuccess;
}

/* MmVirtualGetCurrentDirectory
 * Retrieves the current page-directory for the given cpu */
void*
MmVirtualGetCurrentDirectory(
	_In_ UUId_t Cpu) {
	assert(Cpu < MAX_SUPPORTED_CPUS);
	return (void*)MasterTables[Cpu];
}

/* MmVirtualGetTable
 * Retrieves the relevant table from a virtual address. 
 * Can be created on the fly if specified */
PageTable_t*
MmVirtualGetTable(
	_In_  PageMasterTable_t* PageMasterTable,
	_In_  VirtualAddress_t   VirtualAddress,
    _In_  int                Create,
    _In_  Flags_t            Flags,
    _Out_ int*               Update)
{
	// Variabes
    PageDirectoryTable_t *DirectoryTable    = NULL;
    PageDirectory_t *Directory              = NULL;
	PageTable_t *Table                      = NULL;
	uintptr_t Physical                      = 0;
	int IsCurrent                           = 0;
    
    // Debug
    TRACE("MmVirtualGetTable(VirtualAddress 0x%llx, Create %i)", VirtualAddress, Create);

    // No invalids allowed here
	assert(PageMasterTable != NULL);
	if (MasterTables[CpuGetCurrentId()] == PageMasterTable) {
		IsCurrent = 1;
	}
	MutexLock(&PageMasterTable->Lock);

    // Calculate indices
    int PmIndex     = PAGE_LEVEL_4_INDEX(VirtualAddress);
    int PdpIndex    = PAGE_DIRECTORY_POINTER_INDEX(VirtualAddress);
    int PdIndex     = PAGE_DIRECTORY_INDEX(VirtualAddress);

	// Does page directroy table exist?
	if (!(PageMasterTable->pTables[PmIndex] & PAGE_PRESENT)) {
        if (!Create) {
            TRACE("Page directory table was not present in PML4");
            goto Cleanup;
        }
		DirectoryTable = (PageDirectoryTable_t*)kmalloc_ap(sizeof(PageDirectoryTable_t), &Physical);
        assert(DirectoryTable != NULL);
        memset((void*)DirectoryTable, 0, sizeof(PageDirectoryTable_t));
        PageMasterTable->pTables[PmIndex] = Physical | PAGE_PRESENT | PAGE_WRITE | Flags;
        PageMasterTable->vTables[PmIndex] = (uint64_t)DirectoryTable;
        *Update = IsCurrent;
	}
	DirectoryTable = (PageDirectoryTable_t*)PageMasterTable->vTables[PmIndex];
	assert(DirectoryTable != NULL);

    // Does page directroy exist?
	if (!(DirectoryTable->pTables[PdpIndex] & PAGE_PRESENT)) {
        if (!Create) {
            TRACE("Page directory was not present in the page directory table");
            goto Cleanup;
        }
		Directory = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &Physical);
        assert(Directory != NULL);
        memset((void*)Directory, 0, sizeof(PageDirectory_t));
        DirectoryTable->pTables[PdpIndex] = Physical | PAGE_PRESENT | PAGE_WRITE | Flags;
        DirectoryTable->vTables[PdpIndex] = (uint64_t)Directory;
        *Update = IsCurrent;
	}
	Directory = (PageDirectory_t*)DirectoryTable->vTables[PdpIndex];
	assert(Directory != NULL);

    // Does page table exist?
	if (!(Directory->pTables[PdIndex] & PAGE_PRESENT)) {
        if (!Create) {
            TRACE("Page table was not present in the page directory");
            goto Cleanup;
        }
		Table = (PageTable_t*)kmalloc_ap(sizeof(PageTable_t), &Physical);
        assert(Table != NULL);
        memset((void*)Table, 0, sizeof(PageTable_t));
        Directory->pTables[PdIndex] = Physical | PAGE_PRESENT | PAGE_WRITE | Flags;
        Directory->vTables[PdIndex] = (uint64_t)Table;
        *Update = IsCurrent;
	}
	Table = (PageTable_t*)Directory->vTables[PdIndex];
	assert(Table != NULL);

Cleanup:
	MutexUnlock(&PageMasterTable->Lock);
	return Table;
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
	PageMasterTable_t* PageMasterTable  = (PageMasterTable_t*)PageDirectory;
	PageTable_t *Table                  = NULL;
	int IsCurrent                       = 0;
    int Update                          = 0;

	// Determine page master directory 
	// If we were given null, select the current
	if (PageMasterTable == NULL) {
		PageMasterTable = MasterTables[CpuGetCurrentId()];
	}
	if (MasterTables[CpuGetCurrentId()] == PageMasterTable) {
		IsCurrent = 1;
	}
	assert(PageMasterTable != NULL);
    Table = MmVirtualGetTable(PageMasterTable, vAddress, 0, 0, &Update);
    if (Table == NULL) {
        return OsError;
    }

	// Map it, make sure we mask the page address
	// so we don't accidently set any flags
	Table->Pages[PAGE_TABLE_INDEX(vAddress)] &= PAGE_MASK;
    Table->Pages[PAGE_TABLE_INDEX(vAddress)] |= (Flags & ATTRIBUTE_MASK);

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
	PageMasterTable_t* PageMasterTable  = (PageMasterTable_t*)PageDirectory;
	PageTable_t *Table                  = NULL;
    int Update                          = 0;

	// Determine page master directory 
	// If we were given null, select the current
	if (PageMasterTable == NULL) {
		PageMasterTable = MasterTables[CpuGetCurrentId()];
	}
	assert(PageMasterTable != NULL);
    Table = MmVirtualGetTable(PageMasterTable, vAddress, 0, 0, &Update);
    if (Table == NULL) {
        return OsError;
    }

	// Map it, make sure we mask the page address
	// so we don't accidently set any flags
    if (Flags != NULL) {
        *Flags = Table->Pages[PAGE_TABLE_INDEX(vAddress)] & ATTRIBUTE_MASK;
    }
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
	PageMasterTable_t* PageMasterTable  = (PageMasterTable_t*)PageDirectory;
	PageTable_t *Table                  = NULL;
	int IsCurrent                       = 0;
    int Update                          = 0;

    // Debug
    TRACE("MmVirtualMap(Physical 0x%llx, Virtual 0x%llx, Flags 0x%x)",
        pAddress, vAddress, Flags);

	// Determine page master directory 
	// If we were given null, select the current
	if (PageMasterTable == NULL) {
		PageMasterTable = MasterTables[CpuGetCurrentId()];
	}
	if (MasterTables[CpuGetCurrentId()] == PageMasterTable) {
		IsCurrent = 1;
	}
	assert(PageMasterTable != NULL);
    Table = MmVirtualGetTable(PageMasterTable, vAddress, 1, Flags, &Update);
    if (Table == NULL) {
        WARNING("No table was found or created for virtual address 0x%llx", vAddress);
        return OsError;
    }

    // Trace
    TRACE("After table get/create, update %i", Update);

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

	// Last step is to invalidate the the address in the MMIO
	if (IsCurrent || Update) {
        if (Update) {
            memory_reload_cr3();
        }
        memory_invalidate_addr(vAddress);
	}
	return OsSuccess;
}

/* MmVirtualUnmap
 * Unmaps a previous mapping from the given page-directory
 * the mapping must be present */
OsStatus_t
MmVirtualUnmap(
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   Address)
{
	// Variabes
	PageMasterTable_t* PageMasterTable  = (PageMasterTable_t*)PageDirectory;
	PageTable_t *Table                  = NULL;
    OsStatus_t Result                   = OsSuccess;
	int IsCurrent                       = 0;
    int Update                          = 0;

	// Determine page master directory 
	// If we were given null, select the current
	if (PageMasterTable == NULL) {
		PageMasterTable = MasterTables[CpuGetCurrentId()];
	}
	if (MasterTables[CpuGetCurrentId()] == PageMasterTable) {
		IsCurrent = 1;
	}
	assert(PageMasterTable != NULL);
    Table = MmVirtualGetTable(PageMasterTable, Address, 0, 0, &Update);
    if (Table == NULL) {
        Result = OsError;
		goto Leave;
    }

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
		Table->Pages[PAGE_TABLE_INDEX(Address)] = 0;
		if (!(Physical & PAGE_VIRTUAL)) { // Don't clean virtual mappings
			MmPhysicalFreeBlock(Physical & PAGE_MASK);
		}

		// Last step is to validate the page-mapping
		// now this should be an IPC to all cpu's
		if (IsCurrent) {
			memory_invalidate_addr(Address);
		}
	}

Leave:
	return Result;
}

/* MmVirtualGetMapping
 * Retrieves the physical address mapping of the
 * virtual memory address given - from the page directory that is given */
PhysicalAddress_t
MmVirtualGetMapping(
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   Address)
{
	// Variabes
	PageMasterTable_t* PageMasterTable  = (PageMasterTable_t*)PageDirectory;
    PhysicalAddress_t Mapping           = 0;
	PageTable_t *Table                  = NULL;
    int Update                          = 0;

	// Determine page master directory 
	// If we were given null, select the current
	if (PageMasterTable == NULL) {
		PageMasterTable = MasterTables[CpuGetCurrentId()];
	}
	assert(PageMasterTable != NULL);
    Table = MmVirtualGetTable(PageMasterTable, Address, 0, 0, &Update);
    if (Table == NULL) {
        goto NotMapped;
    }

	// Sanitize the mapping before anything
	if (!(Table->Pages[PAGE_TABLE_INDEX(Address)] & PAGE_PRESENT)) {
		goto NotMapped;
	}

	// Retrieve mapping
	Mapping = Table->Pages[PAGE_TABLE_INDEX(Address)] & PAGE_MASK;
	return (Mapping + (Address & ATTRIBUTE_MASK)); // Return with offset
NotMapped:
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
    uintptr_t MasterAddress     = 0;
    PageMasterTable_t *Created  = (PageMasterTable_t*)kmalloc_ap(sizeof(PageMasterTable_t), &MasterAddress);
    PageMasterTable_t *Current  = MasterTables[CpuGetCurrentId()];

    PageDirectoryTable_t *KernelDirectoryTable  = NULL;
    PageDirectoryTable_t *DirectoryTable        = NULL;
    PageDirectory_t *Directory                  = NULL;

    // Essentially what we want to do here is to clone almost the entire
    // kernel address space (index 0 of the pdp) except for thread region
    // If inherit is set, then clone all other mappings as well
    WARNING("MmVirtualClone(Inherit %i)", Inherit);

    // Lookup which table-region is the stack region
    int ThreadRegion            = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int ApplicationRegionPdp    = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_RING3_CODE);
    int ApplicationRegionPd     = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_CODE);
	
    memset(Created, 0, sizeof(PageMasterTable_t));
	MutexConstruct(&Created->Lock);

    // Get kernel PD[0]
    KernelDirectoryTable = (PageDirectoryTable_t*)KernelMasterTable->vTables[0];

    // PML4[512] => PDP[512] => [PD => PT]
    // Create PML4[0] and PDP[0]
    Created->vTables[0] = (uint64_t)kmalloc_ap(sizeof(PageDirectoryTable_t), &PhysicalAddress);
    Created->pTables[0] = PhysicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    DirectoryTable      = (PageDirectoryTable_t*)Created->vTables[0];
    memset((void*)DirectoryTable, 0, sizeof(PageDirectoryTable_t));

    // Set PD[0] => KERNEL PD[0]
    DirectoryTable->vTables[0] = KernelDirectoryTable->vTables[0];
    DirectoryTable->pTables[0] = KernelDirectoryTable->pTables[0];

    // Set PD[ThreadRegion] => NEW [NON-INHERITABLE]
    DirectoryTable->vTables[ThreadRegion] = (uint64_t)kmalloc_ap(sizeof(PageDirectory_t), &PhysicalAddress);
    DirectoryTable->pTables[ThreadRegion] = PhysicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    Directory                             = (PageDirectory_t*)DirectoryTable->vTables[ThreadRegion];
    memset((void*)Directory, 0, sizeof(PageDirectory_t));
    
    // Then iterate all rest PD[0..511] and copy if Inherit
    // Then iterate all rest PDP[1..511] and copy if Inherit
    // Then iterate all rest PML4[1..511] and copy if Inherit
    if (Inherit) {
        for (int PmIndex = 0; PmIndex < ENTRIES_PER_PAGE; PmIndex++) {
            PageDirectoryTable_t *PdpCreated = (PageDirectoryTable_t*)Created->vTables[PmIndex];
            PageDirectoryTable_t *PdpCurrent = (PageDirectoryTable_t*)Current->vTables[PmIndex];
            for (int PdpIndex = ApplicationRegionPdp; PdpIndex < ENTRIES_PER_PAGE; PdpIndex++) {
                PageDirectory_t *PdCreated = (PageDirectory_t*)PdpCreated->vTables[PdpIndex];
                PageDirectory_t *PdCurrent = (PageDirectory_t*)PdpCurrent->vTables[PdpIndex];
                for (int PdIndex = ApplicationRegionPd; PdIndex < ENTRIES_PER_PAGE; PdIndex++) {
                    if (PdCurrent->pTables[PdIndex]) {
                        PdCreated->pTables[PdIndex] = PdCurrent->pTables[PdIndex] | PAGE_INHERITED;
                        PdCreated->vTables[PdIndex] = PdCurrent->vTables[PdIndex];
                    }
                }

                // Reset to start from zero next iteration
                ApplicationRegionPd = 0;
            }

            // Reset to start from zero next iteration
            ApplicationRegionPdp = 0;
        }
    }

    // Update out's
    *PageDirectory  = (void*)Created;
    *Pdb            = MasterAddress;
    return OsSuccess;
}

/* MmVirtualDestroy
 * Destroys and cleans up any resources used by the virtual address space. */
OsStatus_t
MmVirtualDestroy(
	_In_ void* PageDirectory)
{
    // Variables
    PageMasterTable_t *Current = (PageMasterTable_t*)PageDirectory;

    // Iterate all PML4 entries
    for (int PmIndex = 0; PmIndex < ENTRIES_PER_PAGE; PmIndex++) {
        // Do iteration validation;
        // 1 If index is 0 => not mapped or used
        // 2 If entry equals kernel => skip
        // 3 If entry is inherited => skip
        PageDirectoryTable_t *PdpKernel  = NULL;
        PageDirectoryTable_t *PdpCurrent = NULL;
        
        // Sanitize 1, 2 and 3
        if (Current->pTables[PmIndex] == 0 || 
            (Current->pTables[PmIndex] == KernelMasterTable->pTables[PmIndex]) || 
            (Current->pTables[PmIndex] & PAGE_INHERITED)) {
            continue;
        }

        // Safe Cast indicies
        if (KernelMasterTable->vTables[PmIndex] != 0) {
            PdpKernel = (PageDirectoryTable_t*)KernelMasterTable->vTables[PmIndex];
        }
        PdpCurrent = (PageDirectoryTable_t*)Current->vTables[PmIndex];
        for (int PdpIndex = 0; PdpIndex < ENTRIES_PER_PAGE; PdpIndex++) {
            PageDirectory_t *PdKernel  = NULL;
            PageDirectory_t *PdCurrent = NULL;
            
            // Sanitize 1 and 3
            if (PdpCurrent->pTables[PdpIndex] == 0 || (PdpCurrent->pTables[PdpIndex] & PAGE_INHERITED)) {
                continue;
            }

            // Safe Cast indicies
            if (PdpKernel != NULL) {
                // Sanitize 2
                if (PdpCurrent->pTables[PdpIndex] == PdpKernel->pTables[PdpIndex]) {
                    continue;
                }
                PdKernel = (PageDirectory_t*)PdpKernel->vTables[PdpIndex];
            }
            PdCurrent = (PageDirectory_t*)PdpCurrent->vTables[PdpIndex];
            for (int PdIndex = 0; PdIndex < ENTRIES_PER_PAGE; PdIndex++) {
                PageTable_t *PtKernel  = NULL;
                PageTable_t *PtCurrent = NULL;
                
                // Sanitize 1 and 3
                if (PdCurrent->pTables[PdIndex] == 0 || (PdCurrent->pTables[PdIndex] & PAGE_INHERITED)) {
                    continue;
                }

                // Safe Cast indicies
                if (PdKernel != NULL) {
                    // Sanitize 2
                    if (PdCurrent->pTables[PdIndex] == PdKernel->pTables[PdIndex]) {
                        continue;
                    }
                    PtKernel = (PageTable_t*)PdKernel->vTables[PdIndex];
                }
                PtCurrent = (PageTable_t*)PdCurrent->vTables[PdIndex];
                for (int PtIndex = 0; PtIndex < ENTRIES_PER_PAGE; PtIndex++) {
                    // Sanitize 1 and 3
                    if (PtCurrent->Pages[PtIndex] == 0 || (PtCurrent->Pages[PtIndex] & PAGE_INHERITED)) {
                        continue;
                    }
                    if (PtKernel != NULL) {
                        // Sanitize 2
                        if (PtCurrent->Pages[PtIndex] == PtKernel->Pages[PtIndex]) {
                            continue;
                        }
                    }

                    // Sanitize 4 (Virtual mappings)
                    if (PtCurrent->Pages[PtIndex] & PAGE_VIRTUAL) {
                        continue;
                    }

                    if ((PtCurrent->Pages[PtIndex] & PAGE_MASK) != 0) {
                        if (MmPhysicalFreeBlock(PtCurrent->Pages[PtIndex] & PAGE_MASK) != OsSuccess) {
                            ERROR("Tried to free page %i (0x%x) , but was not allocated", PtIndex, PtCurrent->Pages[PtIndex]);
                        }
                    }
                }

                // Done with page-table, free it
                kfree(PtCurrent);
            }
            
            // Done with page-directory, free it
            kfree(PdCurrent);
        }

        // Done with page-directory-table, free it
        kfree(PdpCurrent);
    }
    // Done with page-master table, free it
    kfree(Current);
    return OsSuccess;
}

/* MmVirtualInitialMap
 * Maps a virtual memory address to a physical memory address in the global master table */
void 
MmVirtualInitialMap(
	_In_ PhysicalAddress_t  pAddress, 
	_In_ VirtualAddress_t   vAddress)
{
	// Variables
	PageDirectoryTable_t *DirectoryTable    = (PageDirectoryTable_t*)KernelMasterTable->vTables[PAGE_LEVEL_4_INDEX(vAddress)];
    PageDirectory_t *Directory              = NULL;
	PageTable_t *Table                      = NULL;

    // Make sure the directory table is present
    if (DirectoryTable == NULL) {
        DirectoryTable = MmVirtualCreatePageDirectoryTable();
        KernelMasterTable->vTables[PAGE_LEVEL_4_INDEX(vAddress)] = (uint64_t)DirectoryTable;
        KernelMasterTable->pTables[PAGE_LEVEL_4_INDEX(vAddress)] = KernelMasterTable->vTables[PAGE_LEVEL_4_INDEX(vAddress)] | PAGE_PRESENT | PAGE_WRITE;
    }
    Directory = (PageDirectory_t*)DirectoryTable->vTables[PAGE_DIRECTORY_POINTER_INDEX(vAddress)];

    // Make sure the directory is present
    if (Directory == NULL) {
        Directory = MmVirtualCreatePageDirectory();
        DirectoryTable->vTables[PAGE_DIRECTORY_POINTER_INDEX(vAddress)] = (uint64_t)Directory;
        DirectoryTable->pTables[PAGE_DIRECTORY_POINTER_INDEX(vAddress)] = DirectoryTable->vTables[PAGE_DIRECTORY_POINTER_INDEX(vAddress)] | PAGE_PRESENT | PAGE_WRITE;
    }
    Table = (PageTable_t*)Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)];

    // Make sure the table is present
    if (Table == NULL) {
        Table = MmVirtualCreatePageTable();
        Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)] = (uint64_t)Table;
        Directory->pTables[PAGE_DIRECTORY_INDEX(vAddress)] = Directory->vTables[PAGE_DIRECTORY_INDEX(vAddress)] | PAGE_PRESENT | PAGE_WRITE;
    }

	// Sanitize no previous mapping exists
	assert(Table->Pages[PAGE_TABLE_INDEX(vAddress)] == 0 && "Dont remap pages without freeing :(");

	// Install the mapping
	Table->Pages[PAGE_TABLE_INDEX(vAddress)] = (pAddress & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE;
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

	// Calculate new address, this is a locked operation
	SpinlockAcquire(&GlobalMemoryLock);
	ReturnAddress = ReservedMemoryPointer;
	ReservedMemoryPointer += (PAGE_SIZE * Pages);
	SpinlockRelease(&GlobalMemoryLock);

	// Done - return address
	return (VirtualAddress_t*)ReturnAddress;
}

/* MmVirtualInit
 * Initializes the virtual memory system and
 * installs default kernel mappings */
OsStatus_t
MmVirtualInit(void)
{
	// Variables
	PageDirectoryTable_t *DirectoryTable    = NULL;
    PageDirectory_t *Directory              = NULL;
	PageTable_t *Table1                     = NULL;
	PageTable_t *Table2                     = NULL;

	AddressSpace_t KernelSpace;

	// Trace information
	TRACE("MmVirtualInit()");

	// Initialize reserved pointer
	ReservedMemoryPointer = MEMORY_LOCATION_RESERVED;

	// Allocate 3 pages for the kernel page directory
	// and reset it by zeroing it out
	KernelMasterTable = (PageMasterTable_t*)MmPhysicalAllocateBlock(MEMORY_ALLOCATION_MASK, 3);
	memset((void*)KernelMasterTable, 0, sizeof(PageMasterTable_t));

    // Allocate rest of resources
    DirectoryTable  = MmVirtualCreatePageDirectoryTable();
    Directory       = MmVirtualCreatePageDirectory();
    Table1          = MmVirtualCreatePageTable();
    Table2          = MmVirtualCreatePageTable();
	MmVirtualFillPageTable(Table1, 0x1000, 0x1000, PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP);
	MmVirtualFillPageTable(Table2, TABLE_SPACE_SIZE, TABLE_SPACE_SIZE, PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP);

    // Create the structure
    // PML4[0] => PDP
    // PDP[0] => PD
    // PD[0] => PT1
    // PD[1] => PT2
    KernelMasterTable->vTables[0]   = (uint64_t)DirectoryTable;
    KernelMasterTable->pTables[0]   = (uint64_t)DirectoryTable | PAGE_PRESENT | PAGE_WRITE;
    DirectoryTable->vTables[0]      = (uint64_t)Directory;
    DirectoryTable->pTables[0]      = (uint64_t)Directory | PAGE_PRESENT | PAGE_WRITE;
    Directory->vTables[0]           = (uint64_t)Table1;
    Directory->pTables[0]           = (uint64_t)Table1 | PAGE_PRESENT | PAGE_WRITE;
    Directory->vTables[1]           = (uint64_t)Table2;
    Directory->pTables[1]           = (uint64_t)Table2 | PAGE_PRESENT | PAGE_WRITE;

	// Initialize locks
	MutexConstruct(&KernelMasterTable->Lock);
	SpinlockReset(&GlobalMemoryLock);

	// Pre-map heap region
	TRACE("Mapping heap region to 0x%x", MEMORY_LOCATION_HEAP);
	MmVirtualIdentityMapMemoryRange(KernelMasterTable, 0, MEMORY_LOCATION_HEAP,
		(MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP), 0, PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP);

	// Pre-map video region
	TRACE("Mapping video memory to 0x%x", MEMORY_LOCATION_VIDEO);
	MmVirtualIdentityMapMemoryRange(KernelMasterTable, VideoGetTerminal()->FrameBufferAddress,
		MEMORY_LOCATION_VIDEO, (VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height),
		1, PAGE_PRESENT | PAGE_WRITE | PAGE_SYSTEM_MAP | PAGE_USER);

	// Install the page table at the reserved system
	// memory, important! 
	TRACE("Mapping reserved memory to 0x%x", MEMORY_LOCATION_RESERVED);

	// Update video address to the new
	VideoGetTerminal()->FrameBufferAddress = MEMORY_LOCATION_VIDEO;

	// Update and switch page-directory for boot-cpu
	MmVirtualSwitchPageDirectory(0, (void*)KernelMasterTable, (uintptr_t)KernelMasterTable);

	// Setup kernel addressing space
	KernelSpace.Flags                       = ASPACE_TYPE_KERNEL;
	KernelSpace.Data[ASPACE_DATA_CR3]       = (uintptr_t)KernelMasterTable;
	KernelSpace.Data[ASPACE_DATA_PDPOINTER] = (uintptr_t)KernelMasterTable;
	return AddressSpaceInitialize(&KernelSpace);
}
