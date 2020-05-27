/**
 * MollenOS
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
 * X86-64 Virtual Memory Manager
 * - Contains the implementation of virtual memory management
 *   for the X86-64 Architecture 
 */

#define __MODULE "MEM1"
//#define __TRACE
#define __COMPILE_ASSERT

#include <arch.h>
#include <assert.h>
#include <handle.h>
#include <heap.h>
#include <debug.h>
#include <machine.h>
#include <memory.h>
#include <memoryspace.h>
#include <string.h>

// Disable the atomic wrong alignment, as they are aligned and are sanitized
// by the static assert
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

PageMasterTable_t*
MmVirtualGetMasterTable(
    _In_  SystemMemorySpace_t*  MemorySpace,
    _In_  VirtualAddress_t      Address,
    _Out_ PageMasterTable_t**   ParentDirectory,
    _Out_ int*                  IsCurrent)
{
    PageMasterTable_t* Directory = (PageMasterTable_t*)MemorySpace->Data[MEMORY_SPACE_DIRECTORY];
    PageMasterTable_t* Parent    = NULL;

	assert(Directory != NULL);

    // If there is no parent then we ignore it as we don't have to synchronize with kernel directory.
    // We always have the shared page-tables mapped. The address must be below the thread-specific space
    if (MemorySpace->ParentHandle != UUID_INVALID) {
        if (Address < MEMORY_LOCATION_RING3_THREAD_START) {
            SystemMemorySpace_t* MemorySpaceParent = (SystemMemorySpace_t*)LookupHandleOfType(
                MemorySpace->ParentHandle, HandleTypeMemorySpace);
            Parent = (PageMasterTable_t*)MemorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
        }
    }

    // Update the provided pointers
    *IsCurrent       = (MemorySpace == GetCurrentMemorySpace()) ? 1 : 0;
    *ParentDirectory = Parent;
    return Directory;
}

PageTable_t*
MmVirtualGetTable(
	_In_  PageMasterTable_t*    ParentPageMasterTable,
	_In_  PageMasterTable_t*    PageMasterTable,
	_In_  VirtualAddress_t      VirtualAddress,
    _In_  int                   IsCurrent,
    _In_  int                   CreateIfMissing,
    _Out_ int*                  Update)
{
    PageDirectoryTable_t* DirectoryTable = NULL;
    PageDirectory_t*      Directory      = NULL;
	PageTable_t*          Table          = NULL;
	uintptr_t             Physical       = 0;
    unsigned int               CreateFlags    = PAGE_PRESENT | PAGE_WRITE;
    uint64_t              ParentMapping;
    int                   Result;
    
    // Initialize indices and variables
    int PmIndex     = PAGE_LEVEL_4_INDEX(VirtualAddress);
    int PdpIndex    = PAGE_DIRECTORY_POINTER_INDEX(VirtualAddress);
    int PdIndex     = PAGE_DIRECTORY_INDEX(VirtualAddress);
    *Update         = 0;
    
    if (VirtualAddress > MEMORY_LOCATION_KERNEL_END) {
        CreateFlags |= PAGE_USER;
    }

    ParentMapping = atomic_load(&PageMasterTable->pTables[PmIndex]);
    
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
            ParentMapping = atomic_load_explicit(&ParentPageMasterTable->pTables[PmIndex], 
                memory_order_acquire);
        }

        // Check the parent-mapping
        if (ParentMapping & PAGE_PRESENT) {
            // Update our page-directory and reload
            atomic_store_explicit(&PageMasterTable->pTables[PmIndex], 
                ParentMapping | PAGETABLE_INHERITED, memory_order_release);
            PageMasterTable->vTables[PmIndex] = ParentPageMasterTable->vTables[PmIndex];
            
            DirectoryTable = (PageDirectoryTable_t*)PageMasterTable->vTables[PmIndex];
            assert(DirectoryTable != NULL);
            *Update = IsCurrent;
        }
        else if (CreateIfMissing) {
            // Allocate, do a CAS and see if it works, if it fails retry our operation
            DirectoryTable = (PageDirectoryTable_t*)kmalloc_p(sizeof(PageDirectoryTable_t), &Physical);
            if (!DirectoryTable) {
                return NULL;
            }
            
            memset((void*)DirectoryTable, 0, sizeof(PageDirectoryTable_t));
            Physical |= CreateFlags;

            // Now perform the synchronization
            if (ParentPageMasterTable != NULL) {
                Result = atomic_compare_exchange_strong(&ParentPageMasterTable->pTables[PmIndex], 
                    &ParentMapping, Physical);
                if (!Result) {
                    // Start over as someone else beat us to the punch
                    kfree((void*)DirectoryTable);
                    goto SyncPmlWithParent;
                }
                
                // If we are inheriting it's important that we mark our copy inherited
                ParentPageMasterTable->vTables[PmIndex] = (uint64_t)DirectoryTable;
                Physical |= PAGETABLE_INHERITED;
            }

            // Update our copy
            atomic_store(&PageMasterTable->pTables[PmIndex], Physical);
            PageMasterTable->vTables[PmIndex] = (uintptr_t)Table;
            *Update                           = IsCurrent;
        }
    }

    // Sanitize the status of the allocation/synchronization
    if (!DirectoryTable) {
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
		Directory = (PageDirectory_t*)kmalloc_p(sizeof(PageDirectory_t), &Physical);
        assert(Directory != NULL);
        memset((void*)Directory, 0, sizeof(PageDirectory_t));

        // Adjust the physical pointer to include flags
        Physical |= CreateFlags;
        Result = atomic_compare_exchange_strong(&DirectoryTable->pTables[PdpIndex], 
                &ParentMapping, Physical);
        if (!Result) {
            // Start over as someone else beat us to the punch
            kfree((void*)Directory);
            goto SyncPdp;
        }

        // Update the pdp
        DirectoryTable->vTables[PdpIndex] = (uint64_t)Directory;
        *Update                           = IsCurrent;
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
		Table = (PageTable_t*)kmalloc_p(sizeof(PageTable_t), &Physical);
        assert(Table != NULL);
        memset((void*)Table, 0, sizeof(PageTable_t));

        // Adjust the physical pointer to include flags
        Physical |= CreateFlags;
        Result = atomic_compare_exchange_strong(&Directory->pTables[PdIndex], 
            &ParentMapping, Physical);
        if (!Result) {
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

OsStatus_t
CloneVirtualSpace(
    _In_ SystemMemorySpace_t*   MemorySpaceParent, 
    _In_ SystemMemorySpace_t*   MemorySpace,
    _In_ int                    Inherit)
{
    PageDirectoryTable_t* SystemDirectoryTable = NULL;
    PageDirectoryTable_t* DirectoryTable       = NULL;
    PageDirectory_t*      Directory            = NULL;
    PageMasterTable_t*    SystemMasterTable    = (PageMasterTable_t*)GetDomainMemorySpace()->Data[MEMORY_SPACE_DIRECTORY];
    PageMasterTable_t*    ParentMasterTable    = NULL;
    PageMasterTable_t*    PageMasterTable;
    uintptr_t PhysicalAddress;
    uintptr_t MasterAddress;

    // Essentially what we want to do here is to clone almost the entire
    // kernel address space (index 0 of the pdp) except for thread region
    // If inherit is set, then clone all other mappings as well
    TRACE("CloneVirtualSpace(Inherit %" PRIiIN ")", Inherit);

    // Lookup which table-region is the stack region
    // We already know the thread-locale region is in PML4[0] => PDP[0] => UNKN
    int ThreadRegion        = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int ApplicationRegion   = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_RING3_CODE);

    PageMasterTable = (PageMasterTable_t*)kmalloc_p(sizeof(PageMasterTable_t), &MasterAddress);
    memset(PageMasterTable, 0, sizeof(PageMasterTable_t));

    // Determine parent
    if (MemorySpaceParent != NULL) {
        ParentMasterTable = (PageMasterTable_t*)MemorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
    }

    // Get kernel PD[0]
    SystemDirectoryTable = (PageDirectoryTable_t*)SystemMasterTable->vTables[0];

    // PML4[512] => PDP[512] => [PD => PT]
    // Create PML4[0] and PDP[0]
    PageMasterTable->vTables[0] = (uint64_t)kmalloc_p(sizeof(PageDirectoryTable_t), &PhysicalAddress);
    atomic_store(&PageMasterTable->pTables[0], PhysicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    DirectoryTable = (PageDirectoryTable_t*)PageMasterTable->vTables[0];
    memset((void*)DirectoryTable, 0, sizeof(PageDirectoryTable_t));

    // Set PD[0] => KERNEL PD[0] and mark as inherited
    PhysicalAddress = atomic_load(&SystemDirectoryTable->pTables[0]);
    atomic_store(&DirectoryTable->pTables[0], PhysicalAddress | PAGETABLE_INHERITED);
    DirectoryTable->vTables[0] = SystemDirectoryTable->vTables[0];

    // Set PD[ThreadRegion] => NEW [NON-INHERITABLE]
    DirectoryTable->vTables[ThreadRegion] = (uint64_t)kmalloc_p(sizeof(PageDirectory_t), &PhysicalAddress);
    atomic_store(&DirectoryTable->pTables[ThreadRegion], PhysicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    Directory = (PageDirectory_t*)DirectoryTable->vTables[ThreadRegion];
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

OsStatus_t
MmVirtualDestroyPageTable(
	_In_ PageTable_t* PageTable)
{
    uint64_t Mapping;

    // Handle PT[0..511] normally
    for (int Index = 0; Index < ENTRIES_PER_PAGE; Index++) {
        Mapping = atomic_load_explicit(&PageTable->Pages[Index], memory_order_relaxed);
        if ((Mapping & PAGE_PERSISTENT) || !(Mapping & PAGE_PRESENT)) {
            continue;
        }

        if ((Mapping & PAGE_MASK) != 0) {
            Mapping &= PAGE_MASK;
            IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
            bounded_stack_push(&GetMachine()->PhysicalMemory, (void*)Mapping);
            IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
        }
    }
    kfree(PageTable);
    return OsSuccess;
}

OsStatus_t
MmVirtualDestroyPageDirectory(
	_In_ PageDirectory_t* PageDirectory)
{
    uint64_t Mapping;

    // Handle PD[0..511] normally
    for (int Index = 0; Index < ENTRIES_PER_PAGE; Index++) {
        Mapping = atomic_load_explicit(&PageDirectory->pTables[Index], memory_order_relaxed);
        if ((Mapping & PAGETABLE_INHERITED) || !(Mapping & PAGE_PRESENT)) {
            continue;
        }
        MmVirtualDestroyPageTable((PageTable_t*)PageDirectory->vTables[Index]);
    }
    kfree(PageDirectory);
    return OsSuccess;
}

OsStatus_t
MmVirtualDestroyPageDirectoryTable(
	_In_ PageDirectoryTable_t* PageDirectoryTable)
{
    uint64_t Mapping;

    // Handle PDP[0..511] normally
    for (int Index = 0; Index < ENTRIES_PER_PAGE; Index++) {
        Mapping = atomic_load_explicit(&PageDirectoryTable->pTables[Index], memory_order_relaxed);
        if ((Mapping & PAGETABLE_INHERITED) || !(Mapping & PAGE_PRESENT)) {
            continue;
        }
        MmVirtualDestroyPageDirectory((PageDirectory_t*)PageDirectoryTable->vTables[Index]);
    }
    kfree(PageDirectoryTable);
    return OsSuccess;
}

OsStatus_t
DestroyVirtualSpace(
    _In_ SystemMemorySpace_t* SystemMemorySpace)
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

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
