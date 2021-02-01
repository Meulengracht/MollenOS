/**
 * MollenOS
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
 * X86-32 Virtual Memory Manager
 * - Contains the implementation of virtual memory management
 *   for the X86-32 Architecture 
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

extern void memory_reload_cr3(void);

// Disable the atomic wrong alignment, as they are aligned and are sanitized
// by the static assert
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

PageDirectory_t*
MmVirtualGetMasterTable(
        _In_  MemorySpace_t*    memorySpace,
        _In_  VirtualAddress_t  address,
        _Out_ PageDirectory_t** parentDirectory,
        _Out_ int*              isCurrentOut)
{
    PageDirectory_t* directory = (PageDirectory_t*)memorySpace->Data[MEMORY_SPACE_DIRECTORY];
    PageDirectory_t* parent    = NULL;

    if (!directory) {
        return NULL;
    }

    // If there is no parent then we ignore it as we don't have to synchronize with kernel directory.
    // We always have the shared page-tables mapped. The address must be below the thread-specific space
    if (memorySpace->ParentHandle != UUID_INVALID) {
        if (address < MEMORY_LOCATION_RING3_THREAD_START) {
            MemorySpace_t * MemorySpaceParent = (MemorySpace_t*)LookupHandleOfType(
                    memorySpace->ParentHandle, HandleTypeMemorySpace);
            parent = (PageDirectory_t*)MemorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
        }
    }

    *isCurrentOut    = (memorySpace == GetCurrentMemorySpace()) ? 1 : 0;
    *parentDirectory = parent;
    return directory;
}

PageTable_t*
MmVirtualGetTable(
    _In_ PageDirectory_t* parentPageDirectory,
    _In_ PageDirectory_t* pageDirectory,
    _In_ uintptr_t        address,
    _In_ int              isCurrent,
    _In_ int              createIfMissing,
    _Out_ int*            update)
{
    PageTable_t* table = NULL;
    int          pageTableIndex = PAGE_DIRECTORY_INDEX(address);
    uint32_t     parentMapping;
    uint32_t     mapping;
    int          result;

    // Load the entry from the table
    mapping = atomic_load(&pageDirectory->pTables[pageTableIndex]);
    *update = 0; // Not used on x32, only 64

    // Sanitize PRESENT status
	if (mapping & PAGE_PRESENT) {
        table = (PageTable_t*)pageDirectory->vTables[pageTableIndex];
	}
	else {
        // Table not present, before attemping to create, sanitize parent
        parentMapping = 0;
        if (parentPageDirectory != NULL) {
            parentMapping = atomic_load(&parentPageDirectory->pTables[pageTableIndex]);
        }

SyncWithParent:
        // Check the parent-mapping
        if (parentMapping & PAGE_PRESENT) {
            // Update our page-directory and reload
            atomic_store(&pageDirectory->pTables[pageTableIndex], parentMapping | PAGETABLE_INHERITED);
            pageDirectory->vTables[pageTableIndex] = parentPageDirectory->vTables[pageTableIndex];
            //smp_mb();
            table = (PageTable_t*)pageDirectory->vTables[pageTableIndex];
        }
        else if (createIfMissing) {
            // Allocate, do a CAS and see if it works, if it fails retry our operation
            uintptr_t tablePhysical;

            table = (PageTable_t*)kmalloc_p(PAGE_SIZE, &tablePhysical);
            if (!table) {
                return NULL;
            }
            
            memset((void*)table, 0, sizeof(PageTable_t));
            tablePhysical |= PAGE_PRESENT | PAGE_WRITE;
            if (address > MEMORY_LOCATION_KERNEL_END) {
                tablePhysical |= PAGE_USER;
            }

            // Now perform the synchronization
            if (parentPageDirectory != NULL) {
                result = atomic_compare_exchange_strong(&parentPageDirectory->pTables[pageTableIndex],
                                                        &parentMapping, tablePhysical);
                if (!result) {
                    // Start over as someone else beat us to the punch
                    kfree((void*)table);
                    goto SyncWithParent;
                }
                
                // Ok we just transferred successfully, mark our copy inheritted
                parentPageDirectory->vTables[pageTableIndex] = (uint32_t)table;
                tablePhysical |= PAGETABLE_INHERITED;
            }

            // Update our copy
            atomic_store(&pageDirectory->pTables[pageTableIndex], tablePhysical);
            pageDirectory->vTables[pageTableIndex] = (uintptr_t)table;
        }

		// Reload CR3 directory to force the MMIO to see our changes 
		if (isCurrent) {
			memory_reload_cr3();
		}
    }
    return table;
}

OsStatus_t
CloneVirtualSpace(
        _In_ MemorySpace_t* memorySpaceParent,
        _In_ MemorySpace_t* memorySpace,
        _In_ int            inherit)
{
    PageDirectory_t* kernelDirectory = (PageDirectory_t*)GetDomainMemorySpace()->Data[MEMORY_SPACE_DIRECTORY];
    PageDirectory_t* parentDirectory = NULL;
    PageDirectory_t* pageDirectory;
    uintptr_t        physicalAddress;
    int              i;

    // Lookup which table-region is the stack region
    int threadRegion    = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_START);
    int threadRegionEnd = PAGE_DIRECTORY_INDEX(MEMORY_LOCATION_RING3_THREAD_END);

    pageDirectory = (PageDirectory_t*)kmalloc_p(sizeof(PageDirectory_t), &physicalAddress);
    if (!pageDirectory) {
        return OsOutOfMemory;
    }
    
    memset(pageDirectory, 0, sizeof(PageDirectory_t));

    // Determine parent
    if (memorySpaceParent != NULL) {
        parentDirectory = (PageDirectory_t*)memorySpaceParent->Data[MEMORY_SPACE_DIRECTORY];
    }

    // Initialize base mappings
    for (i = 0; i < ENTRIES_PER_PAGE; i++) {
        uint32_t kernelMapping, currentMapping;

        // Sanitize stack region, never copy
        if (i >= threadRegion && i <= threadRegionEnd) {
            continue;
        }

        // Sanitize if it's inside kernel region
        if (kernelDirectory->vTables[i] != 0) {
            kernelMapping = atomic_load(&kernelDirectory->pTables[i]);

            atomic_store(&pageDirectory->pTables[i], kernelMapping | PAGETABLE_INHERITED);
            pageDirectory->vTables[i] = kernelDirectory->vTables[i];
            continue;
        }

        // Inherit? We must mark that table inherited to avoid
        // it being freed again
        if (inherit && parentDirectory) {
            currentMapping = atomic_load(&parentDirectory->pTables[i]);
            if (currentMapping & PAGE_PRESENT) {
                atomic_store(&pageDirectory->pTables[i], currentMapping | PAGETABLE_INHERITED);
                pageDirectory->vTables[i] = parentDirectory->vTables[i];
            }
        }
    }

    // Update the configuration data for the memory space
	memorySpace->Data[MEMORY_SPACE_CR3]       = physicalAddress;
    memorySpace->Data[MEMORY_SPACE_DIRECTORY] = (uintptr_t)pageDirectory;

    // Create new resources for the happy new parent :-)
    if (!memorySpaceParent) {
        memorySpace->Data[MEMORY_SPACE_IOMAP] = (uintptr_t)kmalloc(GDT_IOMAP_SIZE);
        if (!memorySpace->Data[MEMORY_SPACE_IOMAP]) {
            // fuck
            kfree(pageDirectory);
            return OsOutOfMemory;
        }

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
DestroyVirtualSpace(
        _In_ MemorySpace_t* memorySpace)
{
    PageDirectory_t* pageDirectory = (PageDirectory_t*)memorySpace->Data[MEMORY_SPACE_DIRECTORY];
    int              i, j;

    // Iterate page-mappings
    for (i = 0; i < ENTRIES_PER_PAGE; i++) {
        PageTable_t* pageTable;
        uint32_t     currentMapping;

        // Do some initial checks on the virtual member to avoid atomics
        // If it's empty or if it's a kernel page table ignore it
        if (pageDirectory->vTables[i] == 0) {
            continue;
        }

        // Load the mapping, then perform checks for inheritation or a system
        // mapping which is done by kernel page-directory
        currentMapping = atomic_load_explicit(&pageDirectory->pTables[i], memory_order_relaxed);
        if ((currentMapping & PAGETABLE_INHERITED) || !(currentMapping & PAGE_PRESENT)) {
            continue;
        }

        // Iterate pages in table
        pageTable = (PageTable_t*)pageDirectory->vTables[i];
        IrqSpinlockAcquire(&GetMachine()->PhysicalMemoryLock);
        for (j = 0; j < ENTRIES_PER_PAGE; j++) {
            currentMapping = atomic_load_explicit(&pageTable->Pages[j], memory_order_relaxed);
            if ((currentMapping & PAGE_PERSISTENT) || !(currentMapping & PAGE_PRESENT)) {
                continue;
            }

            // If it has a mapping - free it
            if ((currentMapping & PAGE_MASK) != 0) {
                currentMapping &= PAGE_MASK;
                bounded_stack_push(&GetMachine()->PhysicalMemory, (void*)currentMapping);
            }
        }
        IrqSpinlockRelease(&GetMachine()->PhysicalMemoryLock);
        kfree(pageTable);
    }
    kfree(pageDirectory);

    // Free the resources allocated specifically for this
    if (memorySpace->ParentHandle == UUID_INVALID) {
        kfree((void*)memorySpace->Data[MEMORY_SPACE_IOMAP]);
    }
    return OsSuccess;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
