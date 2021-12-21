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
#define __TRACE
#define __COMPILE_ASSERT

#include <arch.h>
#include <arch/output.h>
#include <arch/mmu.h>
#include <ddk/io.h>
#include <assert.h>
#include <cpu.h>
#include <debug.h>
#include <gdt.h>
#include <machine.h>
#include <memory.h>
#include <string.h>

extern uintptr_t g_bootMemoryAddress;

uintptr_t g_kernelcr3 = 0;
uintptr_t g_kernelpd  = 0;

STATIC_ASSERT(sizeof(PageDirectory_t) == 8192, Invalid_PageDirectory_Alignment);

#define GET_TABLE_HELPER(MasterTable, Address) ((PageTable_t*)(MasterTable)->vTables[PAGE_DIRECTORY_INDEX(Address)])

// Disable the atomic wrong alignment, as they are aligned and are sanitized
// by the static assert
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

/* MmVirtualCreatePageTable
 * Creates and initializes a new empty page-table */
CREATE_STRUCTURE_HELPER(PageTable_t, PageTable)

static void 
MmVirtualFillPageTable(
        _In_ PageTable_t* pageTable,
        _In_ paddr_t      physicalAddress,
        _In_ vaddr_t      virtualAddress,
        _In_ unsigned int flags,
        _In_ size_t       length)
{
	uintptr_t address = physicalAddress | flags;
	int       ptStart = PAGE_TABLE_INDEX(virtualAddress);
	int       ptEnd   = MIN(ptStart + DIVUP(length, PAGE_SIZE), ENTRIES_PER_PAGE);

	// Iterate through pages and map them
	for (; ptStart < ptEnd; ptStart++, address += PAGE_SIZE) {
        atomic_store(&pageTable->Pages[ptStart], address);
	}
}

static void
MmVirtualMapMemoryRange(
        _In_ PageDirectory_t* pageDirectory,
        _In_ vaddr_t          addressStart,
        _In_ size_t           length,
        _In_ unsigned int     flags)
{
    int pdStart = PAGE_DIRECTORY_INDEX(addressStart);
    int pdEnd   = pdStart + (int)(DIVUP(length, TABLE_SPACE_SIZE));
    int i;

    for (i = pdStart; i < pdEnd; i++) {
        if (pageDirectory->vTables[i] == 0) {
            uintptr_t physicalBase = (uintptr_t)MmVirtualCreatePageTable(&pageDirectory->vTables[i]);
            atomic_store(&pageDirectory->pTables[i], physicalBase | flags);
        }
    }
}

void
MmuPrepareKernel(void)
{
    PageDirectory_t* pageDirectory;
	PageTable_t*     pageTable;
    size_t           bytesToMap;
    paddr_t          physicalBase;
    vaddr_t          virtualBase;
    OsStatus_t       osStatus;
    unsigned int     kernelPageFlags = PAGE_PRESENT | PAGE_WRITE;
	TRACE("MmuPrepareKernel()");

    // Can we use global pages for kernel table?
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
        kernelPageFlags |= PAGE_GLOBAL;
    }

    osStatus = MachineAllocateBootMemory(sizeof(PageDirectory_t),
                                         &virtualBase,
                                         (paddr_t*)&pageDirectory);
    assert(osStatus == OsSuccess);

    memset((void*)pageDirectory, 0, sizeof(PageDirectory_t));
    g_kernelcr3 = (uintptr_t)pageDirectory;
    g_kernelpd  = virtualBase;

    // Due to how it works with multiple cpu's, we need to make sure all shared
    // tables already are mapped in the upper-most level of the page-directory
    TRACE("MmuPrepareKernel pre-mapping kernel region from 0x%" PRIxIN " => 0x%" PRIxIN "",
        0, MEMORY_LOCATION_KERNEL_END);
    
    // Allocate all neccessary memory before starting to identity map
    MmVirtualMapMemoryRange(pageDirectory, 0, MEMORY_LOCATION_KERNEL_END, PAGE_PRESENT | PAGE_WRITE);
    
    // Get the number of reserved bytes - this address is the NEXT page available for
    // allocation, so subtract a page
    bytesToMap = (g_bootMemoryAddress & PAGE_MASK);

    // Do the identity map process for entire 2nd page - LastReservedAddress
    TRACE("MmuPrepareKernel identity mapping 0x%" PRIxIN " => 0x%" PRIxIN "",
        PAGE_SIZE, TABLE_SPACE_SIZE);
    MmVirtualFillPageTable(GET_TABLE_HELPER(pageDirectory, (uint64_t)0),
                           PAGE_SIZE, PAGE_SIZE, kernelPageFlags, TABLE_SPACE_SIZE - PAGE_SIZE);
    virtualBase  = TABLE_SPACE_SIZE;
    physicalBase = TABLE_SPACE_SIZE;
    bytesToMap  -= MIN(bytesToMap, TABLE_SPACE_SIZE);
    
    while (bytesToMap) {
        size_t Length = MIN(bytesToMap, TABLE_SPACE_SIZE);

        pageTable = GET_TABLE_HELPER(pageDirectory, virtualBase);
        TRACE("MmuPrepareKernel identity mapping 0x%" PRIxIN " => 0x%" PRIxIN "",
              virtualBase, virtualBase + Length);
        MmVirtualFillPageTable(pageTable, physicalBase, virtualBase,
                               kernelPageFlags, Length);

        bytesToMap   -= Length;
        physicalBase += Length;
        virtualBase  += Length;
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
