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
        _In_  MemorySpace_t*      memorySpace,
        _In_  VirtualAddress_t    address,
        _Out_ PageMasterTable_t** parentDirectory,
        _Out_ int*                isCurrentOut)
{
    PageMasterTable_t* parent = NULL;

    if (!memorySpace || !parentDirectory || !isCurrentOut) {
        return NULL;
    }

    // If there is no parent then we ignore it as we don't have to synchronize with kernel directory.
    // We always have the shared page-tables mapped. The address must be below the thread-specific space
    if (memorySpace->ParentHandle != UUID_INVALID) {
        if (address < MEMORY_LOCATION_RING3_THREAD_START) {
            MemorySpace_t * MemorySpaceParent = (MemorySpace_t*)LookupHandleOfType(
                    memorySpace->ParentHandle, HandleTypeMemorySpace);
            parent = (PageMasterTable_t*)MemorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
        }
    }

    // Update the provided pointers
    *isCurrentOut    = (memorySpace == GetCurrentMemorySpace()) ? 1 : 0;
    *parentDirectory = parent;
    return (PageMasterTable_t*)memorySpace->Data[MEMORY_SPACE_DIRECTORY];
}

static PageDirectoryTable_t*
__GetPageDirectoryTable(
        _In_  PageMasterTable_t* parentPageMasterTable,
        _In_  PageMasterTable_t* pageMasterTable,
        _In_  VirtualAddress_t   virtualAddress,
        _In_  int                isCurrent,
        _In_  unsigned int       createFlags,
        _In_  int                createIfMissing,
        _Out_ int*               update)
{
    PageDirectoryTable_t* directoryTable = NULL;
    int                   pmIndex        = PAGE_LEVEL_4_INDEX(virtualAddress);
    uint64_t              parentMapping  = atomic_load(&pageMasterTable->pTables[pmIndex]);

    /////////////////////////////////////////////////
    // Check and synchronize the page-directory table as that is the
    // only level that needs to be sync between parent and child
    if (parentMapping & PAGE_PRESENT) {
        directoryTable = (PageDirectoryTable_t*)pageMasterTable->vTables[pmIndex];
    }
    else {
        // Table not present, before attemping to create, sanitize parent
SyncPmlWithParent:
        parentMapping = 0;
        if (parentPageMasterTable != NULL) {
            parentMapping = atomic_load_explicit(&parentPageMasterTable->pTables[pmIndex],
                                                 memory_order_acquire);
        }

        // Check the parent-mapping, if we have a parent-mapping then mark it INHERITED
        if (parentMapping & PAGE_PRESENT) {
            // Update our page-directory and reload
            atomic_store_explicit(&pageMasterTable->pTables[pmIndex],
                                  parentMapping | PAGETABLE_INHERITED, memory_order_release);
            pageMasterTable->vTables[pmIndex] = parentPageMasterTable->vTables[pmIndex];
            directoryTable = (PageDirectoryTable_t*)pageMasterTable->vTables[pmIndex];

            // set update when any changes happen to our current memory space
            *update = isCurrent;
        }
        else if (createIfMissing) {
            uintptr_t physical;

            // Allocate, do a CAS and see if it works, if it fails retry our operation
            directoryTable = (PageDirectoryTable_t*)kmalloc_p(sizeof(PageDirectoryTable_t), &physical);
            if (!directoryTable) {
                return NULL;
            }

            memset((void*)directoryTable, 0, sizeof(PageDirectoryTable_t));
            physical |= createFlags;

            // Now perform the synchronization with our parent, and in case we have a parent
            // we must mark our entry as INHERITED, as the parent will have the responsibility to free stuff
            if (parentPageMasterTable != NULL) {
                int result = atomic_compare_exchange_strong(&parentPageMasterTable->pTables[pmIndex],
                                                        &parentMapping, physical);
                if (!result) {
                    // Start over as someone else beat us to the punch
                    kfree((void*)directoryTable);
                    goto SyncPmlWithParent;
                }

                // If we are inheriting it's important that we mark our copy inherited
                parentPageMasterTable->vTables[pmIndex] = (uint64_t)directoryTable;
                physical |= PAGETABLE_INHERITED;
            }

            // Update our copy
            atomic_store(&pageMasterTable->pTables[pmIndex], physical);
            pageMasterTable->vTables[pmIndex] = (uintptr_t)directoryTable;
            *update = isCurrent;
        }
    }

    return directoryTable;
}

PageTable_t*
MmVirtualGetTable(
	_In_  PageMasterTable_t* parentPageMasterTable,
	_In_  PageMasterTable_t* pageMasterTable,
	_In_  VirtualAddress_t   virtualAddress,
    _In_  int                isCurrent,
    _In_  int                createIfMissing,
    _Out_ int*               update)
{
    PageDirectoryTable_t* directoryTable;
    PageDirectory_t*      directory      = NULL;
	PageTable_t*          table          = NULL;
	uintptr_t             physical       = 0;
    unsigned int          createFlags    = PAGE_PRESENT | PAGE_WRITE;
    uint64_t              mapping;
    int                   result;

    int pdpIndex = PAGE_DIRECTORY_POINTER_INDEX(virtualAddress);
    int pdIndex  = PAGE_DIRECTORY_INDEX(virtualAddress);

    if (!pageMasterTable || !update) {
        return NULL;
    }

    // Modify the creation flags, we need to change them in a few cases
    // 1) If we are mapping any address above kernel region, it needs PAGE_USER
    if (virtualAddress > MEMORY_LOCATION_KERNEL_END) {
        createFlags |= PAGE_USER;
    }

    *update = 0;
    directoryTable = __GetPageDirectoryTable(parentPageMasterTable, pageMasterTable, virtualAddress, isCurrent,
                                             createFlags, createIfMissing, update);
    if (!directoryTable) {
        return NULL;
    }

    // The rest of the levels (Page-Directories and Page-Tables) are now
    // in shared access, which means multiple accesses to these instances will
    // be done. If there are ANY changes it must be with LOAD/MODIFY/CAS to make
    // sure changes are atomic. These changes does NOT need to be propegated upwards to parent
    mapping = atomic_load(&directoryTable->pTables[pdpIndex]);
SyncPdp:
    if (mapping & PAGE_PRESENT) {
        directory = (PageDirectory_t*)directoryTable->vTables[pdpIndex];
    }
    else if (createIfMissing) {
        directory = (PageDirectory_t*)kmalloc_p(sizeof(PageDirectory_t), &physical);
        assert(directory != NULL);
        memset((void*)directory, 0, sizeof(PageDirectory_t));

        // Adjust the physical pointer to include flags
        physical |= createFlags;
        result = atomic_compare_exchange_strong(
                &directoryTable->pTables[pdpIndex], &mapping, physical);
        if (!result) {
            // Start over as someone else beat us to the punch
            kfree((void*)directory);
            goto SyncPdp;
        }

        // Update the pdp
        directoryTable->vTables[pdpIndex] = (uint64_t)directory;
        *update = isCurrent;
    }

    // Sanitize the status of the allocation/synchronization
    if (directory == NULL) {
        return NULL;
    }

    mapping = atomic_load(&directory->pTables[pdIndex]);
SyncPd:
    if (mapping & PAGE_PRESENT) {
        table = (PageTable_t*)directory->vTables[pdIndex];
        assert(table != NULL);
    }
    else if (createIfMissing) {
        table  = (PageTable_t*)kmalloc_p(sizeof(PageTable_t), &physical);
        assert(table != NULL);
        memset((void*)table, 0, sizeof(PageTable_t));

        // Adjust the physical pointer to include flags
        physical |= createFlags;
        result = atomic_compare_exchange_strong(
                &directory->pTables[pdIndex], &mapping, physical);
        if (!result) {
            // Start over as someone else beat us to the punch
            kfree((void*)table);
            goto SyncPd;
        }

        // Update the pd
        directory->vTables[pdIndex] = (uint64_t)table;
        *update = isCurrent;
    }
	return table;
}

OsStatus_t
CloneVirtualSpace(
        _In_ MemorySpace_t* parentMemorySpace,
        _In_ MemorySpace_t* memorySpace,
        _In_ int            inherit)
{
    PageMasterTable_t*    kernelMasterTable = (PageMasterTable_t*)GetDomainMemorySpace()->Data[MEMORY_SPACE_DIRECTORY];
    PageMasterTable_t*    parentMasterTable;
    PageMasterTable_t*    pageMasterTable;
    PageDirectoryTable_t* directoryTable;
    uintptr_t             physicalAddress;
    uintptr_t             masterAddress;
    TRACE("CloneVirtualSpace(Inherit %" PRIiIN ")", inherit);

    // lookup and sanitize regions
    int sharedPml4Entry      = PAGE_LEVEL_4_INDEX(MEMORY_LOCATION_KERNEL);
    int applicationPml4Entry = PAGE_LEVEL_4_INDEX(MEMORY_LOCATION_RING3_CODE);
    int threadPml4Entry      = PAGE_LEVEL_4_INDEX(MEMORY_LOCATION_RING3_THREAD_START);

    assert(sharedPml4Entry != applicationPml4Entry);
    assert(sharedPml4Entry != threadPml4Entry);

    // Determine parent
    if (parentMemorySpace != NULL) {
        parentMasterTable = (PageMasterTable_t*)parentMemorySpace->Data[MEMORY_SPACE_DIRECTORY];
    }

    // create the new PML4
    pageMasterTable = (PageMasterTable_t*)kmalloc_p(sizeof(PageMasterTable_t), &masterAddress);
    if (!pageMasterTable) {
        return OsOutOfMemory;
    }
    memset(pageMasterTable, 0, sizeof(PageMasterTable_t));

    // transfer over the shared pml4 entry and mark inherited, we are not allowed to free anything on cleanup
    physicalAddress = atomic_load(&kernelMasterTable->pTables[sharedPml4Entry]);
    pageMasterTable->pTables[sharedPml4Entry] = physicalAddress | PAGETABLE_INHERITED;
    pageMasterTable->vTables[sharedPml4Entry] = kernelMasterTable->vTables[sharedPml4Entry];

    // create the new thread-specific pml4 entry
    directoryTable = (PageDirectoryTable_t*)kmalloc_p(sizeof(PageDirectoryTable_t), &physicalAddress);
    if (!directoryTable) {
        kfree(pageMasterTable);
        return OsOutOfMemory;
    }
    memset((void*)directoryTable, 0, sizeof(PageDirectoryTable_t));
    pageMasterTable->vTables[threadPml4Entry] = (uint64_t)directoryTable;
    atomic_store(&pageMasterTable->pTables[threadPml4Entry], physicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

    // inherit all application pml4 entries
    if (inherit && parentMasterTable) {
        for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
            if (i == sharedPml4Entry || i == threadPml4Entry) {
                continue; // skip thread and kernel
            }

            physicalAddress = atomic_load(&parentMasterTable->pTables[i]);
            if (physicalAddress & PAGE_PRESENT) {
                atomic_store(&pageMasterTable->pTables[i], physicalAddress | PAGETABLE_INHERITED);
                pageMasterTable->vTables[i] = parentMasterTable->vTables[i];
            }
        }
    }

    // Update the configuration data for the memory space
	memorySpace->Data[MEMORY_SPACE_CR3]       = masterAddress;
    memorySpace->Data[MEMORY_SPACE_DIRECTORY] = (uintptr_t)pageMasterTable;

    // Create new resources for the happy new parent :-)
    if (!parentMemorySpace) {
        memorySpace->Data[MEMORY_SPACE_IOMAP] = (uintptr_t)kmalloc(GDT_IOMAP_SIZE);
        if (!memorySpace->Data[MEMORY_SPACE_IOMAP]) {
            // crap
            kfree((void*)pageMasterTable->vTables[threadPml4Entry]);
            kfree(pageMasterTable);
            return OsOutOfMemory;
        }

        // For application memory spaces the IO map has all ports disabled, otherwise enabled
        if (memorySpace->Flags & MEMORY_SPACE_APPLICATION) {
            memset((void*)memorySpace->Data[MEMORY_SPACE_IOMAP], 0xFF, GDT_IOMAP_SIZE);
        }
        else {
            memset((void*)memorySpace->Data[MEMORY_SPACE_IOMAP], 0, GDT_IOMAP_SIZE);
        }
    }
    return OsSuccess;
}

OsStatus_t
MmVirtualDestroyPageTable(
	_In_ PageTable_t* pageTable)
{
    // Handle PT[0..511] normally
    IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
    for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
        uint64_t mapping = atomic_load_explicit(&pageTable->Pages[i], memory_order_relaxed);
        uint64_t address = mapping & PAGE_MASK;
        if (!address || (mapping & PAGE_PERSISTENT) || !(mapping & PAGE_PRESENT)) {
            continue;
        }

        bounded_stack_push(&GetMachine()->PhysicalMemory, (void*)address);
    }
    IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
    kfree(pageTable);
    return OsSuccess;
}

OsStatus_t
MmVirtualDestroyPageDirectory(
	_In_ PageDirectory_t* pageDirectory)
{
    // Handle PD[0..511] normally
    for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
        uint64_t mapping = atomic_load_explicit(&pageDirectory->pTables[i], memory_order_relaxed);
        if ((mapping & PAGETABLE_INHERITED) || !(mapping & PAGE_PRESENT)) {
            continue;
        }
        MmVirtualDestroyPageTable((PageTable_t*)pageDirectory->vTables[i]);
    }
    kfree(pageDirectory);
    return OsSuccess;
}

OsStatus_t
MmVirtualDestroyPageDirectoryTable(
	_In_ PageDirectoryTable_t* pageDirectoryTable)
{
    // Handle PDP[0..511] normally, usually the first entry will be the kernel space which is inherited
    for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
        uint64_t mapping = atomic_load_explicit(&pageDirectoryTable->pTables[i], memory_order_relaxed);
        if ((mapping & PAGETABLE_INHERITED) || !(mapping & PAGE_PRESENT)) {
            continue;
        }
        MmVirtualDestroyPageDirectory((PageDirectory_t*)pageDirectoryTable->vTables[i]);
    }
    kfree(pageDirectoryTable);
    return OsSuccess;
}

OsStatus_t
DestroyVirtualSpace(
        _In_ MemorySpace_t* memorySpace)
{
    PageMasterTable_t* current = (PageMasterTable_t*)memorySpace->Data[MEMORY_SPACE_DIRECTORY];

    // Handle PML4[0..511] normally, the PML4 itself always needs cleaning up as every thread
    // has their own version
    for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
        uint64_t mapping = atomic_load_explicit(&current->pTables[i], memory_order_relaxed);

        // In most cases only the first one will not be inherrited, but the rest of the PML4 entries
        // could in theory be inherrited.
        if ((mapping & PAGE_PRESENT) && !(mapping & PAGETABLE_INHERITED)) {
            MmVirtualDestroyPageDirectoryTable((PageDirectoryTable_t*)current->vTables[i]);
        }
    }
    kfree(current);

    // Free the resources allocated specifically for this
    if (memorySpace->ParentHandle == UUID_INVALID) {
        kfree((void*)memorySpace->Data[MEMORY_SPACE_IOMAP]);
    }
    return OsSuccess;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
