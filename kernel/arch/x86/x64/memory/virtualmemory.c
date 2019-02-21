/* MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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

#include <component/cpu.h>
#include <memoryspace.h>
#include <system/output.h>
#include <system/utils.h>
#include <threading.h>
#include <handle.h>
#include <memory.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <arch.h>
#include <apic.h>
#include <cpu.h>
#include <gdt.h>

extern OsStatus_t SwitchVirtualSpace(SystemMemorySpace_t*);

// Function helpers for repeating functions where it pays off
// to have them seperate
#define CREATE_STRUCTURE_HELPER(Type, Name) Type* MmVirtualCreate##Name(void) { \
                                            Type *Instance = (Type*)AllocateSystemMemory(DIVUP(sizeof(Type), PAGE_SIZE), MEMORY_ALLOCATION_MASK, 0); \
                                            assert(Instance != NULL); \
                                            memset((void*)Instance, 0, sizeof(Type)); \
                                            return Instance; }
#define GET_TABLE_HELPER(MasterTable, Address) ((PageTable_t*)((PageDirectory_t*)((PageDirectoryTable_t*)MasterTable->vTables[PAGE_LEVEL_4_INDEX(Address)])->vTables[PAGE_DIRECTORY_POINTER_INDEX(Address)])->vTables[PAGE_DIRECTORY_INDEX(Address)])


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
        atomic_store(&Table->Pages[PAGE_TABLE_INDEX(VirtualEntry)], PhysicalEntry | Flags);
	}
}


/* MmVirtualMapMemoryRange
 * Maps an entire region into the given page-directory and marks them present.
 * They can then be used for either identity mapping or real mappings afterwards. */
void 
MmVirtualMapMemoryRange(
	_In_ PageMasterTable_t* MasterTable,
	_In_ VirtualAddress_t   AddressStart,
	_In_ uintptr_t          Length,
	_In_ Flags_t            Flags)
{
	// Variables
    PageDirectoryTable_t *DirectoryTable;
    PageDirectory_t *Directory;
	unsigned i, j, je, k, ke;

    // Get indices, need them to make decisions
    int PmStart     = PAGE_LEVEL_4_INDEX(AddressStart);
    int PmEnd       = PAGE_LEVEL_4_INDEX(AddressStart + Length - 1) + 1;

    int PdpStart    = PAGE_DIRECTORY_POINTER_INDEX(AddressStart);
    int PdpEnd      = PAGE_DIRECTORY_POINTER_INDEX(AddressStart + Length - 1) + 1;

    int PdStart     = PAGE_DIRECTORY_INDEX(AddressStart);
    int PdEnd       = PAGE_DIRECTORY_INDEX(AddressStart + Length - 1) + 1;

    // Iterate all the neccessary page-directory tables
    // @todo the indices are not correct for sub pml4
    for (i = PmStart; i < PmEnd; i++)
    {
        if (MasterTable->vTables[i] == 0) {
            MasterTable->vTables[i] = (uintptr_t)MmVirtualCreatePageDirectoryTable();
            atomic_store_explicit(&MasterTable->pTables[i], MasterTable->vTables[i] | Flags, 
                memory_order_relaxed);
        }
        DirectoryTable = (PageDirectoryTable_t*)MasterTable->vTables[i];

        // Iterate all the neccessary page-directories
        j   = (i == PmStart) ? PdpStart : 0;
        je  = ((i + 1) == PmEnd) ? PdpEnd : ENTRIES_PER_PAGE;
        for (; j < je; j++)
        {
            if (DirectoryTable->vTables[j] == 0) {
                DirectoryTable->vTables[j] = (uintptr_t)MmVirtualCreatePageDirectory();
                atomic_store_explicit(&DirectoryTable->pTables[j], DirectoryTable->vTables[j] | Flags, 
                    memory_order_relaxed);
            }
            Directory = (PageDirectory_t*)DirectoryTable->vTables[j];

            // Iterate all the page-tables that will be needed
            k   = (j == PdpStart) ? PdStart : 0;
            ke  = ((j + 1) == PdpEnd) ? PdEnd : ENTRIES_PER_PAGE;
            for (; k < ke; k++)
            {
                if (Directory->vTables[k] == 0) {
                    Directory->vTables[k] = (uintptr_t)MmVirtualCreatePageTable();
                    atomic_store_explicit(&Directory->pTables[k], Directory->vTables[k] | Flags, 
                        memory_order_relaxed);
                }
            }
        }
    }
}

/* MmVirtualGetMasterTable
 * Helper function to retrieve the current active PML4. */
PageMasterTable_t*
MmVirtualGetMasterTable(
    _In_  SystemMemorySpace_t*  MemorySpace,
    _In_  VirtualAddress_t      Address,
    _Out_ PageMasterTable_t**   ParentDirectory,
    _Out_ int*                  IsCurrent)
{
    // Variables
    PageMasterTable_t *Directory    = (PageMasterTable_t*)MemorySpace->Data[MEMORY_SPACE_DIRECTORY];
    PageMasterTable_t *Parent       = NULL;

	assert(Directory != NULL);

    // If there is no parent then we ignore it as we don't have to synchronize with kernel directory.
    // We always have the shared page-tables mapped. The address must be below the thread-specific space
    if (MemorySpace->ParentHandle != UUID_INVALID) {
        if (Address < MEMORY_LOCATION_RING3_THREAD_START) {
            SystemMemorySpace_t* MemorySpaceParent = (SystemMemorySpace_t*)LookupHandle(MemorySpace->ParentHandle);
            Parent = (PageMasterTable_t*)MemorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
        }
    }

    // Update the provided pointers
    *IsCurrent          = (MemorySpace == GetCurrentMemorySpace()) ? 1 : 0;
    *ParentDirectory    = Parent;
    return Directory;
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
            atomic_store(&PageMasterTable->pTables[PmIndex], ParentMapping | PAGETABLE_INHERITED);
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
            Physical |= PAGETABLE_INHERITED;
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

/* CloneVirtualSpace
 * Clones a new virtual memory space for an application to use. */
OsStatus_t
CloneVirtualSpace(
    _In_ SystemMemorySpace_t*   MemorySpaceParent, 
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ int                    Inherit)
{
    // Variables
    PageDirectoryTable_t *SystemDirectoryTable  = NULL;
    PageDirectoryTable_t *DirectoryTable        = NULL;
    PageDirectory_t *Directory                  = NULL;
    PageMasterTable_t *SystemMasterTable = (PageMasterTable_t*)GetDomainMemorySpace()->Data[MEMORY_SPACE_DIRECTORY];
    PageMasterTable_t *ParentMasterTable = NULL;
    PageMasterTable_t *PageMasterTable;
    uintptr_t PhysicalAddress;
    uintptr_t MasterAddress;

    // Essentially what we want to do here is to clone almost the entire
    // kernel address space (index 0 of the pdp) except for thread region
    // If inherit is set, then clone all other mappings as well
    TRACE("CloneVirtualSpace(Inherit %i)", Inherit);

    // Lookup which table-region is the stack region
    // We already know the thread-locale region is in PML4[0] => PDP[0] => UNKN
    int ThreadRegion        = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int ApplicationRegion   = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_RING3_CODE);

    PageMasterTable = (PageMasterTable_t*)kmalloc_ap(sizeof(PageMasterTable_t), &MasterAddress);
    memset(PageMasterTable, 0, sizeof(PageMasterTable_t));

    // Determine parent
    if (MemorySpaceParent != NULL) {
        ParentMasterTable = (PageMasterTable_t*)MemorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
    }

    // Get kernel PD[0]
    SystemDirectoryTable = (PageDirectoryTable_t*)SystemMasterTable->vTables[0];

    // PML4[512] => PDP[512] => [PD => PT]
    // Create PML4[0] and PDP[0]
    PageMasterTable->vTables[0] = (uint64_t)kmalloc_ap(sizeof(PageDirectoryTable_t), &PhysicalAddress);
    atomic_store(&PageMasterTable->pTables[0], PhysicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    DirectoryTable      = (PageDirectoryTable_t*)PageMasterTable->vTables[0];
    memset((void*)DirectoryTable, 0, sizeof(PageDirectoryTable_t));

    // Set PD[0] => KERNEL PD[0]
    PhysicalAddress = atomic_load(&SystemDirectoryTable->pTables[0]);
    atomic_store(&DirectoryTable->pTables[0], PhysicalAddress);
    DirectoryTable->vTables[0] = SystemDirectoryTable->vTables[0];

    // Set PD[ThreadRegion] => NEW [NON-INHERITABLE]
    DirectoryTable->vTables[ThreadRegion] = (uint64_t)kmalloc_ap(sizeof(PageDirectory_t), &PhysicalAddress);
    atomic_store(&DirectoryTable->pTables[ThreadRegion], PhysicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    Directory                             = (PageDirectory_t*)DirectoryTable->vTables[ThreadRegion];
    memset((void*)Directory, 0, sizeof(PageDirectory_t));
    
    // Then iterate all rest PD[0..511] and copy if Inherit
    // Then iterate all rest PDP[1..511] and copy if Inherit
    // Then iterate all rest PML4[1..511] and copy if Inherit
    if (Inherit && ParentMasterTable != NULL) {
        // Handle PML4[0] a little different as we can't just copy it
        PageDirectoryTable_t *DirectoryTableCurrent = (PageDirectoryTable_t*)ParentMasterTable->vTables[0];
        for (int PdpIndex = ApplicationRegion; PdpIndex < ENTRIES_PER_PAGE; PdpIndex++) {
            uint64_t Mapping = atomic_load(&DirectoryTableCurrent->pTables[PdpIndex]);
            if (Mapping & PAGE_PRESENT) {
                atomic_store(&DirectoryTable->pTables[PdpIndex], Mapping | PAGETABLE_INHERITED);
                DirectoryTable->vTables[PdpIndex] = DirectoryTableCurrent->vTables[PdpIndex];
            }
        }

        // Handle rest by just copying all the remaining PML4 entries
        // @todo flexibility
        for (int PmIndex = 1; PmIndex < ENTRIES_PER_PAGE; PmIndex++) {
            uint64_t Mapping = atomic_load(&ParentMasterTable->pTables[PmIndex]);
            if (Mapping & PAGE_PRESENT) {
                atomic_store(&PageMasterTable->pTables[PmIndex], Mapping | PAGETABLE_INHERITED);
                PageMasterTable->vTables[PmIndex] = ParentMasterTable->vTables[PmIndex];
            }
        }
    }

    // Update the configuration data for the memory space
	MemorySpace->Data[MEMORY_SPACE_CR3]       = MasterAddress;
	MemorySpace->Data[MEMORY_SPACE_DIRECTORY] = (uintptr_t)PageMasterTable;

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
        Mapping = atomic_load_explicit(&PageTable->Pages[Index], memory_order_relaxed);
        if ((Mapping & (PAGE_SYSTEM_MAP | PAGE_PERSISTENT)) || 
            !(Mapping & PAGE_PRESENT)) {
            continue;
        }

        if ((Mapping & PAGE_MASK) != 0) {
            if (FreeSystemMemory(Mapping & PAGE_MASK, PAGE_SIZE) != OsSuccess) {
                ERROR("Tried to free page %i (0x%x), but was not allocated", Index, Mapping);
            }
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
        Mapping = atomic_load_explicit(&PageDirectory->pTables[Index], memory_order_relaxed);
        if (Mapping & (PAGE_SYSTEM_MAP | PAGETABLE_INHERITED)) {
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
        Mapping = atomic_load_explicit(&PageDirectoryTable->pTables[Index], memory_order_relaxed);
        if (Mapping & (PAGE_SYSTEM_MAP | PAGETABLE_INHERITED)) {
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

/* DestroyVirtualSpace
 * Destroys and cleans up any resources used by the virtual address space. */
OsStatus_t
DestroyVirtualSpace(
    _In_ SystemMemorySpace_t*   SystemMemorySpace)
{
    PageMasterTable_t *Current = (PageMasterTable_t*)SystemMemorySpace->Data[MEMORY_SPACE_DIRECTORY];
    uint64_t Mapping;

    // Handle PML4[0..511] normally
    for (int PmIndex = 0; PmIndex < ENTRIES_PER_PAGE; PmIndex++) {
        Mapping = atomic_load_explicit(&Current->pTables[PmIndex], memory_order_relaxed);
        if (Mapping & PAGE_PRESENT) {
            MmVirtualDestroyPageDirectoryTable((PageDirectoryTable_t*)Current->vTables[PmIndex]);
        }
    }
    kfree(Current);

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
    PageMasterTable_t *iDirectory;
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

	// Allocate 2 pages for the kernel page directory
	// and reset it by zeroing it out
    if (GetCurrentProcessorCore() == &GetMachine()->Processor.PrimaryCore) {
        size_t BytesToMap               = 0;
        PhysicalAddress_t PhysicalBase  = 0;
        VirtualAddress_t VirtualBase    = 0;

        iDirectory = (PageMasterTable_t*)AllocateSystemMemory(
            sizeof(PageMasterTable_t), MEMORY_ALLOCATION_MASK, 0);
        memset((void*)iDirectory, 0, sizeof(PageMasterTable_t));
        iPhysical = (uintptr_t)iDirectory;

        // Due to how it works with multiple cpu's, we need to make sure all shared
        // tables already are mapped in the upper-most level of the page-directory
        TRACE("Mapping the kernel region from 0x%x => 0x%x", MEMORY_LOCATION_KERNEL, MEMORY_LOCATION_KERNEL_END);
        MmVirtualMapMemoryRange(iDirectory, 0, MEMORY_LOCATION_KERNEL_END, KernelPageFlags);

        // Identity map some of the regions
        // Kernel image region
        // Kernel video region
        MmVirtualFillPageTable(GET_TABLE_HELPER(iDirectory, (uint64_t)0), 0x1000, 0x1000, KernelPageFlags);
        MmVirtualFillPageTable(GET_TABLE_HELPER(iDirectory, (uint64_t)TABLE_SPACE_SIZE), TABLE_SPACE_SIZE, TABLE_SPACE_SIZE, KernelPageFlags);
        BytesToMap      = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;
        PhysicalBase    = VideoGetTerminal()->FrameBufferAddress;
        VirtualBase     = MEMORY_LOCATION_VIDEO;
        while (BytesToMap) {
            iTable          = GET_TABLE_HELPER(iDirectory, VirtualBase);
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
    }
    else {
        // Create a new page directory but copy all kernel mappings to the domain specific memory
        iDirectory = (PageMasterTable_t*)kmalloc_ap(sizeof(PageMasterTable_t), &iPhysical);
        NOTIMPLEMENTED("Implement initialization of other-domain virtaul spaces");
    }
    return OsSuccess;
}
