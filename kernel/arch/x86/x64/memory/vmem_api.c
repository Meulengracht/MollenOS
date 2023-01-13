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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * X86-64 Virtual Memory Manager
 * - Contains the implementation of virtual memory management
 *   for the X86-64 Architecture 
 */

#define __MODULE "MEM1"
//#define __TRACE
#define __need_static_assert

#include <arch/x86/arch.h>
#include <arch/x86/memory.h>
#include <assert.h>
#include <handle.h>
#include <heap.h>
#include <debug.h>
#include <machine.h>
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
        _In_  vaddr_t    address,
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
            parent = (PageMasterTable_t*)MemorySpaceParent->PlatformData.Cr3VirtualAddress;
        }
    }

    // Update the provided pointers
    *isCurrentOut    = (memorySpace == GetCurrentMemorySpace()) ? 1 : 0;
    *parentDirectory = parent;
    return (PageMasterTable_t*)memorySpace->PlatformData.Cr3VirtualAddress;
}

static PageDirectoryTable_t*
__GetPageDirectoryTable(
        _In_  PageMasterTable_t* parentPageMasterTable,
        _In_  PageMasterTable_t* pageMasterTable,
        _In_  vaddr_t   virtualAddress,
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

                // If we are inheriting its important that we mark our copy inherited
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
        _In_  vaddr_t            virtualAddress,
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
    if (virtualAddress >= MEMORY_LOCATION_RING3_CODE) {
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
    // sure changes are atomic. These changes do NOT need to be propegated upwards to parent
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

static oserr_t
__CloneKernelDirectory(
        _In_ PageMasterTable_t* source,
        _In_ PageMasterTable_t* destination)
{
    PageDirectoryTable_t* sourcePdp;
    PageDirectoryTable_t* destinationPdp;
    paddr_t               physicalAddress;
    vaddr_t               virtualAddress;

    int kernelPml4Entry;
    int kernelSharedEntry;
    int kernelTlsEntry;

    kernelPml4Entry   = PAGE_LEVEL_4_INDEX(MEMORY_LOCATION_KERNEL);
    kernelSharedEntry = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_KERNEL);
    kernelTlsEntry    = PAGE_DIRECTORY_POINTER_INDEX(MEMORY_LOCATION_TLS_START);

    // get a pointer to the source Pdp where we will copy the shared entry
    sourcePdp = (PageDirectoryTable_t*)source->vTables[kernelPml4Entry];
    if (!sourcePdp) {
        ERROR("__CloneKernelDirectory kernel PML4 was not present in source directory!");
        return OS_EINVALPARAMS;
    }

    // create the pml4 entry, we create a custom copy of this for each PML4.
    destinationPdp = (PageDirectoryTable_t*)kmalloc_p(sizeof(PageDirectoryTable_t), &physicalAddress);
    if (!destinationPdp) {
        return OS_EOOM;
    }
    memset(destinationPdp, 0, sizeof(PageDirectoryTable_t));

    // install the PML4[kernel_data] entry
    atomic_store(&destination->pTables[kernelPml4Entry], physicalAddress | PAGE_WRITE | PAGE_PRESENT);
    destination->vTables[kernelPml4Entry] = (uint64_t)destinationPdp;

    // transfer over the shared pml4 entry and mark inherited, we are not allowed to free anything on cleanup
    physicalAddress = atomic_load(&sourcePdp->pTables[kernelSharedEntry]) | PAGETABLE_INHERITED;
    atomic_store(&destinationPdp->pTables[kernelSharedEntry], physicalAddress);
    destinationPdp->vTables[kernelSharedEntry] = sourcePdp->vTables[kernelSharedEntry];

    // install the kernelTlsEntry
    virtualAddress = (vaddr_t)kmalloc_p(sizeof(PageDirectory_t), &physicalAddress);
    if (!virtualAddress) {
        kfree(destinationPdp);
        return OS_EOOM;
    }
    memset((void*)virtualAddress, 0, sizeof(PageDirectory_t));
    atomic_store(&destinationPdp->pTables[kernelTlsEntry], physicalAddress | PAGE_PRESENT | PAGE_WRITE);
    destinationPdp->vTables[kernelTlsEntry] = virtualAddress;
    return OS_EOK;
}

oserr_t
MmVirtualClone(
        _In_  MemorySpace_t* source,
        _In_  int            inherit,
        _Out_ paddr_t*       cr3Out,
        _Out_ vaddr_t*       pdirOut)
{
    PageMasterTable_t*    kernelMasterTable = (PageMasterTable_t*)GetDomainMemorySpace()->PlatformData.Cr3VirtualAddress;
    PageMasterTable_t*    sourceMasterTable;
    PageMasterTable_t*    pageMasterTable;
    PageDirectoryTable_t* directoryTable;
    uintptr_t             physicalAddress;
    uintptr_t             masterAddress;
    oserr_t               osStatus;
    TRACE("MmuCloneVirtualSpace(inherit=%" PRIiIN ")", inherit);

    // lookup and sanitize regions
    int kernelPml4Entry  = PAGE_LEVEL_4_INDEX(MEMORY_LOCATION_KERNEL);
    int appDataPml4Entry = PAGE_LEVEL_4_INDEX(MEMORY_LOCATION_RING3_CODE);
    int appTlsPml4Entry  = PAGE_LEVEL_4_INDEX(MEMORY_LOCATION_RING3_THREAD_START);

    assert(kernelPml4Entry != appDataPml4Entry);
    assert(kernelPml4Entry != appTlsPml4Entry);

    if (source != NULL) {
        sourceMasterTable = (PageMasterTable_t*)source->PlatformData.Cr3VirtualAddress;
    }

    // create the new PML4
    pageMasterTable = (PageMasterTable_t*)kmalloc_p(sizeof(PageMasterTable_t), &masterAddress);
    if (!pageMasterTable) {
        return OS_EOOM;
    }
    memset(pageMasterTable, 0, sizeof(PageMasterTable_t));

    // transfer over the shared pml4 entry and mark inherited, we are not allowed to free anything on cleanup
    osStatus = __CloneKernelDirectory(kernelMasterTable, pageMasterTable);
    if (osStatus != OS_EOK) {
        kfree(pageMasterTable);
        return osStatus;
    }

    // create the new thread-specific pml4 application entry
    directoryTable = (PageDirectoryTable_t*)kmalloc_p(sizeof(PageDirectoryTable_t), &physicalAddress);
    if (!directoryTable) {
        kfree(pageMasterTable);
        return OS_EOOM;
    }
    memset((void*)directoryTable, 0, sizeof(PageDirectoryTable_t));
    pageMasterTable->vTables[appTlsPml4Entry] = (uint64_t)directoryTable;
    atomic_store(&pageMasterTable->pTables[appTlsPml4Entry], physicalAddress | PAGE_PRESENT | PAGE_WRITE | PAGE_USER);

    // inherit all application pml4 entries
    if (inherit && sourceMasterTable) {
        for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
            if (i == kernelPml4Entry || i == appTlsPml4Entry) {
                continue; // skip thread and kernel
            }

            physicalAddress = atomic_load(&sourceMasterTable->pTables[i]);
            if (physicalAddress & PAGE_PRESENT) {
                atomic_store(&pageMasterTable->pTables[i], physicalAddress | PAGETABLE_INHERITED);
                pageMasterTable->vTables[i] = sourceMasterTable->vTables[i];
            }
        }
    }
    *cr3Out  = masterAddress;
    *pdirOut = (uintptr_t)pageMasterTable;
    return OS_EOK;
}

oserr_t
MmVirtualDestroyPageTable(
	_In_ PageTable_t* pageTable)
{
    // Handle PT[0..511] normally
    for (int i = 0; i < ENTRIES_PER_PAGE; i++) {
        uint64_t mapping = atomic_load_explicit(&pageTable->Pages[i], memory_order_relaxed);
        uint64_t address = mapping & PAGE_MASK;
        if (!address || (mapping & PAGE_PERSISTENT) || !(mapping & PAGE_PRESENT)) {
            continue;
        }
        FreePhysicalMemory(1, &address);
    }
    kfree(pageTable);
    return OS_EOK;
}

oserr_t
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
    return OS_EOK;
}

oserr_t
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
    return OS_EOK;
}

oserr_t
MmuDestroyVirtualSpace(
        _In_ MemorySpace_t* memorySpace)
{
    PageMasterTable_t* current = (PageMasterTable_t*)memorySpace->PlatformData.Cr3VirtualAddress;

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
        kfree((void*)memorySpace->PlatformData.TssIoMap);
    }
    return OS_EOK;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
