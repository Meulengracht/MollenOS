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
 * Needed for the virtual memory manager to keep track of current directories */
static PageMasterTable_t *KernelMasterTable     = NULL;
static _Atomic(uintptr_t) ReservedMemoryPointer = ATOMIC_VAR_INIT(0);
static MemorySynchronizationObject_t SyncData   = { SPINLOCK_INIT, 0 };

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
    _In_ PageMasterTable_t* ParentDirectory,
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
        atomic_store(&Table->Pages[PAGE_TABLE_INDEX(VirtualEntry)], PhysicalEntry | Flags);
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
    uint64_t Mapping;

    // Must be page-table aligned
    assert((PhysicalAddressStart % TABLE_SPACE_SIZE) == 0);
    assert((VirtualAddressStart % TABLE_SPACE_SIZE) == 0);

    // Determine start + end PML4 Index
    int PmIndexStart    = PAGE_LEVEL_4_INDEX(VirtualAddress);
    int PmIndexEnd      = PAGE_LEVEL_4_INDEX(VirtualAddress + Length - 1) + 1;
    for (int PmIndex = PmIndexStart; PmIndex < PmIndexEnd; PmIndex++) {
        // Sanitize existance/mapping
        if (!PageMaster->vTables[PmIndex]) {
            Mapping                         = (uint64_t)MmVirtualCreatePageDirectoryTable();
            PageMaster->vTables[PmIndex]    = Mapping;
            atomic_store(&PageMaster->pTables[PmIndex], Mapping | Flags);
        }
        DirectoryTable = (PageDirectoryTable_t*)PageMaster->vTables[PmIndex];

        // Determine start + end of the page directory pointer index
        int PdpIndexStart   = PAGE_DIRECTORY_POINTER_INDEX(VirtualAddress);
        int PdpIndexEnd     = PAGE_DIRECTORY_POINTER_INDEX(VirtualAddress + Length - 1) + 1;
        for (int PdpIndex = PdpIndexStart; PdpIndex < PdpIndexEnd; PdpIndex++) {
            // Sanitize existance/mapping
            if (!DirectoryTable->vTables[PdpIndex]) {
                Mapping                             = (uint64_t)MmVirtualCreatePageDirectory();
                DirectoryTable->vTables[PdpIndex]   = Mapping;
                atomic_store(&DirectoryTable->pTables[PdpIndex], Mapping | Flags);
            }
            Directory = (PageDirectory_t*)DirectoryTable->vTables[PdpIndex];

            int PdIndexStart    = PAGE_DIRECTORY_INDEX(VirtualAddress);
            int PdIndexEnd      = PAGE_DIRECTORY_INDEX(VirtualAddress + Length - 1) + 1;
            for (int PdIndex = PdIndexStart; PdIndex < PdIndexEnd; PdIndex++) {
                // Sanitize existance/mapping
                if (!Directory->vTables[PdIndex]) {
                    Mapping                     = (uint64_t)MmVirtualCreatePageTable();
                    Directory->vTables[PdIndex] = Mapping;
                    atomic_store(&Directory->pTables[PdIndex], Mapping | Flags);
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

    // Now we can use the local apic to get id
    GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR] = (uintptr_t)KernelMasterTable;
}

/* MmVirtualGetMasterTable
 * Helper function to retrieve the current active PML4. */
PageMasterTable_t*
MmVirtualGetMasterTable(
    _In_  void*             PageDirectory,
    _Out_ int*              IsCurrent)
{
    // Variables
    PageMasterTable_t *MasterTable = (PageMasterTable_t*)PageDirectory;

	// Determine page directory 
	// If we were given null, select the current
	if (MasterTable == NULL) {
		MasterTable = (PageMasterTable_t*)GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR];
        *IsCurrent = 1;
	}
	else if ((PageMasterTable_t*)GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR] == MasterTable) {
		*IsCurrent = 1;
	}
	assert(MasterTable != NULL);
    return MasterTable;
}

/* MmVirtualGetTable
 * Helper function to retrieve a table from the given master table. */
PageTable_t*
MmVirtualGetTable(
	_In_  PageMasterTable_t*    ParentPageMasterTable,
	_In_  PageMasterTable_t*    PageMasterTable,
	_In_  VirtualAddress_t      VirtualAddress,
    _In_  int                   IsCurrent,
    _In_  int                   CreateIfMissing,
    _In_  Flags_t               CreateFlags,
    _Out_ int*                  Update)
{
	// Variabes
    PageDirectoryTable_t *DirectoryTable    = NULL;
    PageDirectory_t *Directory              = NULL;
	PageTable_t *Table                      = NULL;
	uintptr_t Physical                      = 0;
    uint64_t ParentMapping;

    // Initialize indices and variables
    int PmIndex     = PAGE_LEVEL_4_INDEX(VirtualAddress);
    int PdpIndex    = PAGE_DIRECTORY_POINTER_INDEX(VirtualAddress);
    int PdIndex     = PAGE_DIRECTORY_INDEX(VirtualAddress);
    ParentMapping   = atomic_load(&PageMasterTable->pTables[PmIndex]);
    *Update         = 0;

	/////////////////////////////////////////////////
    // Check and synchronize the page-directory table as that is the 
    // only level that needs to be sync between parent and child
    if (ParentMapping & PAGE_PRESENT) {
        DirectoryTable = (PageDirectoryTable_t*)PageMasterTable->vTables[PmIndex];
        assert(DirectoryTable != NULL);
    }
    else {
        // Table not present, before attemping to create, sanitize parent
SyncPmlWithParent:
        ParentMapping = 0;
        if (ParentPageMasterTable != NULL) {
            ParentMapping = atomic_load(&ParentPageMasterTable->pTables[PmIndex]);
        }

        // Check the parent-mapping
        if (ParentMapping & PAGE_PRESENT) {
            // Update our page-directory and reload
            atomic_store(&PageMasterTable->pTables[PmIndex], ParentMapping | PAGE_INHERITED);
            PageMasterTable->vTables[PmIndex]   = ParentPageMasterTable->vTables[PmIndex];
            DirectoryTable                      = (PageDirectoryTable_t*)PageMasterTable->vTables[PmIndex];
            assert(DirectoryTable != NULL);
            *Update                             = IsCurrent;
        }
        else if (CreateIfMissing) {
            // Allocate, do a CAS and see if it works, if it fails retry our operation
            DirectoryTable = (PageDirectoryTable_t*)kmalloc_ap(sizeof(PageDirectoryTable_t), &Physical);
            assert(DirectoryTable != NULL);
            memset((void*)DirectoryTable, 0, sizeof(PageDirectoryTable_t));

            // Now perform the synchronization
            Physical |= PAGE_PRESENT | PAGE_WRITE | CreateFlags;
            if (ParentPageMasterTable != NULL && !atomic_compare_exchange_strong(
                &ParentPageMasterTable->pTables[PmIndex], &ParentMapping, Physical)) {
                // Start over as someone else beat us to the punch
                kfree((void*)DirectoryTable);
                goto SyncPmlWithParent;
            }

            // Update us and mark our copy inherited
            Physical |= PAGE_INHERITED;
            atomic_store(&PageMasterTable->pTables[PmIndex], Physical);
            PageMasterTable->vTables[PmIndex]   = (uintptr_t)Table;
            *Update                             = IsCurrent;
        }
    }

    // Sanitize the status of the allocation/synchronization
    if (DirectoryTable == NULL) {
        return NULL;
    }

	/////////////////////////////////////////////////
    // The rest of the levels (Page-Directories and Page-Tables) are now
    // in shared access, which means multiple accesses to these instances will
    // be done. If there are ANY changes it must be with LOAD/MODIFY/CAS to make
    // sure changes are atomic. These changes does NOT need to be propegated upwards to parent
    ParentMapping = atomic_load(&DirectoryTable->pTables[PdpIndex]);
SyncPdp:
    if (ParentMapping & PAGE_PRESENT) {
        Directory = (PageDirectory_t*)DirectoryTable->vTables[PdpIndex];
        assert(Directory != NULL);
    }
    else if (CreateIfMissing) {
		Directory = (PageDirectory_t*)kmalloc_ap(sizeof(PageDirectory_t), &Physical);
        assert(Directory != NULL);
        memset((void*)Directory, 0, sizeof(PageDirectory_t));

        // Adjust the physical pointer to include flags
        Physical |= PAGE_PRESENT | PAGE_WRITE | CreateFlags;
        if (!atomic_compare_exchange_strong(&DirectoryTable->pTables[PdpIndex], 
            &ParentMapping, Physical)) {
            // Start over as someone else beat us to the punch
            kfree((void*)Directory);
            goto SyncPdp;
        }

        // Update the pdp
        DirectoryTable->vTables[PdpIndex]   = (uint64_t)Directory;
        *Update                             = IsCurrent;
    }

    // Sanitize the status of the allocation/synchronization
    if (Directory == NULL) {
        return NULL;
    }

    ParentMapping = atomic_load(&Directory->pTables[PdIndex]);
SyncPd:
    if (ParentMapping & PAGE_PRESENT) {
        Table = (PageTable_t*)Directory->vTables[PdIndex];
        assert(Table != NULL);
    }
    else if (CreateIfMissing) {
		Table = (PageTable_t*)kmalloc_ap(sizeof(PageTable_t), &Physical);
        assert(Table != NULL);
        memset((void*)Table, 0, sizeof(PageTable_t));

        // Adjust the physical pointer to include flags
        Physical |= PAGE_PRESENT | PAGE_WRITE | CreateFlags;
        if (!atomic_compare_exchange_strong(&Directory->pTables[PdIndex], 
            &ParentMapping, Physical)) {
            // Start over as someone else beat us to the punch
            kfree((void*)Table);
            goto SyncPd;
        }

        // Update the pd
        Directory->vTables[PdIndex] = (uint64_t)Table;
        *Update                     = IsCurrent;
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
	int IsCurrent                   = 0;
    int Update                      = 0;
    PageMasterTable_t* ParentTable  = (PageMasterTable_t*)ParentPageDirectory;
	PageMasterTable_t* MasterTable  = NULL;
	PageTable_t *Table              = NULL;
    uint64_t Mapping;

    // Retrieve both the current master-table and do a table lookup (safe)
    MasterTable = MmVirtualGetMasterTable(PageDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentTable, MasterTable, vAddress, IsCurrent, 0, 0, &Update);

    // Sanitize the table
    if (Table == NULL) {
        return OsError;
    }

	// Map it, make sure we mask the page address so we don't accidently set any flags
    Mapping = atomic_load(&Table->Pages[PAGE_TABLE_INDEX(vAddress)]);
    if (!(Mapping & PAGE_SYSTEM_MAP)) {
        atomic_store(&Table->Pages[PAGE_TABLE_INDEX(vAddress)], (Mapping & PAGE_MASK) | (Flags & ATTRIBUTE_MASK));

        // Last step is to invalidate the the address in the MMIO
        MmVirtualSynchronizePage(ParentTable, vAddress);
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
    _In_ void*              ParentPageDirectory,
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   vAddress, 
	_In_ Flags_t*           Flags)
{
	// Variabes
	int IsCurrent                   = 0;
    int Update                      = 0;
    PageMasterTable_t* ParentTable  = (PageMasterTable_t*)ParentPageDirectory;
	PageMasterTable_t* MasterTable  = NULL;
	PageTable_t *Table              = NULL;

    // Retrieve both the current master-table and do a table lookup (safe)
    MasterTable = MmVirtualGetMasterTable(PageDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentTable, MasterTable, vAddress, IsCurrent, 0, 0, &Update);

    // Sanitize the table
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
 * Installs a new page-mapping in the given
 * page-directory. The type of mapping is controlled by
 * the Flags parameter. */
OsStatus_t
MmVirtualMap(
    _In_ void*              ParentPageDirectory,
	_In_ void*              PageDirectory, 
	_In_ PhysicalAddress_t  pAddress, 
	_In_ VirtualAddress_t   vAddress, 
	_In_ Flags_t            Flags)
{
	// Variabes
	int IsCurrent                   = 0;
    int Update                      = 0;
    PageMasterTable_t* ParentTable  = (PageMasterTable_t*)ParentPageDirectory;
	PageMasterTable_t* MasterTable  = NULL;
	PageTable_t *Table              = NULL;
    uint64_t Mapping;

    // For kernel mappings we would like to mark the mappings global
    if (vAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            Flags |= PAGE_GLOBAL;
        }  
    }

    // Retrieve both the current master-table and do a table lookup (safe)
    MasterTable = MmVirtualGetMasterTable(PageDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentTable, MasterTable, vAddress, IsCurrent, 1, Flags, &Update);
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
        return OsError;
    }

    // Perform the mapping in a weak context, fast operation
    if (!atomic_compare_exchange_weak(&Table->Pages[PAGE_TABLE_INDEX(vAddress)], 
        &Mapping, (pAddress & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | Flags)) {
        goto SyncTable;
    }

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
    _In_ void*              ParentPageDirectory,
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   Address)
{
	// Variabes
	int IsCurrent                   = 0;
    int Update                      = 0;
    PageMasterTable_t* ParentTable  = (PageMasterTable_t*)ParentPageDirectory;
	PageMasterTable_t* MasterTable  = NULL;
	PageTable_t *Table              = NULL;
    uint64_t Mapping;

    // Retrieve both the current master-table and do a table lookup (safe)
    MasterTable = MmVirtualGetMasterTable(PageDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentTable, MasterTable, Address, IsCurrent, 0, 0, &Update);

    // Sanitize table status
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
                MmPhysicalFreeBlock(Mapping & PAGE_MASK);
            }

            // Last step is to validate the page-mapping
            // now this should be an IPC to all cpu's
            MmVirtualSynchronizePage(ParentTable, Address);
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
	int IsCurrent                   = 0;
    int Update                      = 0;
    PageMasterTable_t* ParentTable  = (PageMasterTable_t*)ParentPageDirectory;
	PageMasterTable_t* MasterTable  = NULL;
	PageTable_t *Table              = NULL;
    uint64_t Mapping;

    // Retrieve both the current master-table and do a table lookup (safe)
    MasterTable = MmVirtualGetMasterTable(PageDirectory, &IsCurrent);
    Table       = MmVirtualGetTable(ParentTable, MasterTable, Address, IsCurrent, 0, 0, &Update);

    // Sanitize table status
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
    uintptr_t MasterAddress     = 0;
    PageMasterTable_t *Created  = (PageMasterTable_t*)kmalloc_ap(sizeof(PageMasterTable_t), &MasterAddress);
    PageMasterTable_t *Current  = (PageMasterTable_t*)GetCurrentProcessorCore()->Data[CPUCORE_DATA_VIRTUAL_DIR];
    
    PageDirectoryTable_t *KernelDirectoryTable  = NULL;
    PageDirectoryTable_t *DirectoryTable        = NULL;
    PageDirectory_t *Directory                  = NULL;

    // Essentially what we want to do here is to clone almost the entire
    // kernel address space (index 0 of the pdp) except for thread region
    // If inherit is set, then clone all other mappings as well
    TRACE("MmVirtualClone(Inherit %i)", Inherit);

    // Lookup which table-region is the stack region
    // We already know the thread-locale region is in PML4[0] => PDP[0] => UNKN
    int ThreadRegion        = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int ApplicationRegion   = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_RING3_CODE);
	
    memset(Created, 0, sizeof(PageMasterTable_t));

    // Get kernel PD[0]
    KernelDirectoryTable = (PageDirectoryTable_t*)KernelMasterTable->vTables[0];

    // PML4[512] => PDP[512] => [PD => PT]
    // Create PML4[0] and PDP[0]
    Created->vTables[0] = (uint64_t)kmalloc_ap(sizeof(PageDirectoryTable_t), &PhysicalAddress);
    atomic_store(&Created->pTables[0], PhysicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    DirectoryTable      = (PageDirectoryTable_t*)Created->vTables[0];
    memset((void*)DirectoryTable, 0, sizeof(PageDirectoryTable_t));

    // Set PD[0] => KERNEL PD[0]
    PhysicalAddress = atomic_load(&KernelDirectoryTable->pTables[0]);
    atomic_store(&DirectoryTable->pTables[0], PhysicalAddress);
    DirectoryTable->vTables[0] = KernelDirectoryTable->vTables[0];

    // Set PD[ThreadRegion] => NEW [NON-INHERITABLE]
    DirectoryTable->vTables[ThreadRegion] = (uint64_t)kmalloc_ap(sizeof(PageDirectory_t), &PhysicalAddress);
    atomic_store(&DirectoryTable->pTables[ThreadRegion], PhysicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    Directory                             = (PageDirectory_t*)DirectoryTable->vTables[ThreadRegion];
    memset((void*)Directory, 0, sizeof(PageDirectory_t));
    
    // Then iterate all rest PD[0..511] and copy if Inherit
    // Then iterate all rest PDP[1..511] and copy if Inherit
    // Then iterate all rest PML4[1..511] and copy if Inherit
    if (Inherit) {
        // Handle PML4[0] a little different as we can't just copy it
        PageDirectoryTable_t *DirectoryTableCurrent = (PageDirectoryTable_t*)Current->vTables[0];
        for (int PdpIndex = ApplicationRegion; PdpIndex < ENTRIES_PER_PAGE; PdpIndex++) {
            uint64_t Mapping = atomic_load(&DirectoryTableCurrent->pTables[PdpIndex]);
            if (Mapping & PAGE_PRESENT) {
                atomic_store(&DirectoryTable->pTables[PdpIndex], Mapping | PAGE_INHERITED);
                DirectoryTable->vTables[PdpIndex] = DirectoryTableCurrent->vTables[PdpIndex];
            }
        }

        // Handle rest by just copying all the remaining PML4 entries
        // @todo flexibility
        for (int PmIndex = 1; PmIndex < ENTRIES_PER_PAGE; PmIndex++) {
            uint64_t Mapping = atomic_load(&Current->pTables[PmIndex]);
            if (Mapping & PAGE_PRESENT) {
                atomic_store(&Created->pTables[PmIndex], Mapping | PAGE_INHERITED);
                Created->vTables[PmIndex] = Current->vTables[PmIndex];
            }
        }
    }

    // Update out's
    *PageDirectory  = (void*)Created;
    *Pdb            = MasterAddress;
    return OsSuccess;
}

/* MmVirtualDestroyPageTable
 * Iterates entries in a page-directory and cleans up structures and entries. */
OsStatus_t
MmVirtualDestroyPageTable(
	_In_ PageTable_t* PageTable)
{
    // Variables
    uint64_t Mapping;

    // Handle PT[0..511] normally
    for (int Index = 0; Index < ENTRIES_PER_PAGE; Index++) {
        Mapping = atomic_load(&PageTable->Pages[Index]);
        if ((Mapping & PAGE_INHERITED) || (Mapping & PAGE_SYSTEM_MAP)) {
            continue;
        }

        if (Mapping & PAGE_PRESENT) {
            MmPhysicalFreeBlock(Mapping & PAGE_MASK);
        }
    }

    // Done with page-table, free it
    kfree(PageTable);
    return OsSuccess;
}

/* MmVirtualDestroyPageDirectory
 * Iterates entries in a page-directory and cleans up structures and entries. */
OsStatus_t
MmVirtualDestroyPageDirectory(
	_In_ PageDirectory_t* PageDirectory)
{
    // Variables
    uint64_t Mapping;

    // Handle PD[0..511] normally
    for (int Index = 0; Index < ENTRIES_PER_PAGE; Index++) {
        Mapping = atomic_load(&PageDirectory->pTables[Index]);
        if ((Mapping & PAGE_INHERITED) || (Mapping & PAGE_SYSTEM_MAP)) {
            continue;
        }

        if (Mapping & PAGE_PRESENT) {
            MmVirtualDestroyPageTable((PageTable_t*)PageDirectory->vTables[Index]);
        }
    }

    // Done with page-directory, free it
    kfree(PageDirectory);
    return OsSuccess;
}

/* MmVirtualDestroyPageDirectoryTable
 * Iterates entries in a page-directory-table and cleans up structures and entries. */
OsStatus_t
MmVirtualDestroyPageDirectoryTable(
	_In_ PageDirectoryTable_t* PageDirectoryTable)
{
    // Variables
    uint64_t Mapping;

    // Handle PDP[0..511] normally
    for (int Index = 0; Index < ENTRIES_PER_PAGE; Index++) {
        Mapping = atomic_load(&PageDirectoryTable->pTables[Index]);
        if ((Mapping & PAGE_INHERITED) || (Mapping & PAGE_SYSTEM_MAP)) {
            continue;
        }

        if (Mapping & PAGE_PRESENT) {
            MmVirtualDestroyPageDirectory((PageDirectory_t*)PageDirectoryTable->vTables[Index]);
        }
    }

    // Done with page-directory-table, free it
    kfree(PageDirectoryTable);
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
    uint64_t Mapping;

    // Handle PML4[0..511] normally
    for (int PmIndex = 0; PmIndex < ENTRIES_PER_PAGE; PmIndex++) {
        Mapping = atomic_load(&Current->pTables[PmIndex]);
        if (Mapping & PAGE_PRESENT) {
            MmVirtualDestroyPageDirectoryTable((PageDirectoryTable_t*)Current->vTables[PmIndex]);
        }
    }

    // Done with page-master table, free it
    kfree(Current);
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
	PageDirectoryTable_t *DirectoryTable    = NULL;
    PageDirectory_t *Directory              = NULL;
	PageTable_t *Table1                     = NULL;
	PageTable_t *Table2                     = NULL;
    Flags_t KernelPageFlags                 = 0;

	AddressSpace_t KernelSpace;

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
	KernelMasterTable = (PageMasterTable_t*)MmPhysicalAllocateBlock(MEMORY_ALLOCATION_MASK, 2);
	memset((void*)KernelMasterTable, 0, sizeof(PageMasterTable_t));
    memset((void*)&KernelSpace, 0, sizeof(AddressSpace_t));

    // Allocate rest of resources
    DirectoryTable  = MmVirtualCreatePageDirectoryTable();
    Directory       = MmVirtualCreatePageDirectory();
    Table1          = MmVirtualCreatePageTable();
    Table2          = MmVirtualCreatePageTable();
	MmVirtualFillPageTable(Table1, 0x1000, 0x1000, KernelPageFlags);
	MmVirtualFillPageTable(Table2, TABLE_SPACE_SIZE, TABLE_SPACE_SIZE, KernelPageFlags);

    // Create the structure
    // PML4[0] => PDP
    // PDP[0] => PD
    // PD[0] => PT1
    // PD[1] => PT2
    KernelMasterTable->vTables[0]   = (uint64_t)DirectoryTable;
    atomic_store(&KernelMasterTable->pTables[0], (uint64_t)DirectoryTable | KernelPageFlags);
    DirectoryTable->vTables[0]      = (uint64_t)Directory;
    atomic_store(&DirectoryTable->pTables[0], (uint64_t)Directory | KernelPageFlags);
    Directory->vTables[0]           = (uint64_t)Table1;
    atomic_store(&Directory->pTables[0], (uint64_t)Table1 | KernelPageFlags);
    Directory->vTables[1]           = (uint64_t)Table2;
    atomic_store(&Directory->pTables[1], (uint64_t)Table2 | KernelPageFlags);

	// Pre-map heap region
	TRACE("Mapping heap region to 0x%x", MEMORY_LOCATION_HEAP);
	MmVirtualIdentityMapMemoryRange(KernelMasterTable, 0, MEMORY_LOCATION_HEAP,
		(MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP), 0, KernelPageFlags);

	// Pre-map video region
	TRACE("Mapping video memory to 0x%x", MEMORY_LOCATION_VIDEO);
	MmVirtualIdentityMapMemoryRange(KernelMasterTable, VideoGetTerminal()->FrameBufferAddress,
		MEMORY_LOCATION_VIDEO, (VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height),
		1, KernelPageFlags | PAGE_USER); // @todo is PAGE_USER neccessary?

	// Install the page table at the reserved system
	// memory, important! 
	TRACE("Mapping reserved memory to 0x%x", MEMORY_LOCATION_RESERVED);

	// Update video address to the new
	VideoGetTerminal()->FrameBufferAddress = MEMORY_LOCATION_VIDEO;

	// Update and switch page-directory for boot-cpu
	UpdateVirtualAddressingSpace((void*)KernelMasterTable, (uintptr_t)KernelMasterTable);

	// Setup kernel addressing space
	KernelSpace.Flags                       = ASPACE_TYPE_KERNEL;
	KernelSpace.Data[ASPACE_DATA_CR3]       = (uintptr_t)KernelMasterTable;
	KernelSpace.Data[ASPACE_DATA_PDPOINTER] = (uintptr_t)KernelMasterTable;
	return AddressSpaceInitialize(&KernelSpace);
}
