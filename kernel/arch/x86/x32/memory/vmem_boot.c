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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * X86-32 Virtual Memory Manager
 * - Contains the implementation of virtual memory management
 *   for the X86-32 Architecture 
 */

#define __MODULE "MEM1"
#define __TRACE
#define __need_static_assert
#define __need_quantity
#define __need_minmax
#include <arch/x86/arch.h>
#include <arch/x86/cpu.h>
#include <arch/x86/memory.h>
#include <assert.h>
#include <debug.h>
#include <machine.h>
#include <string.h>

uintptr_t g_kernelcr3 = 0;
uintptr_t g_kernelpd  = 0;

STATIC_ASSERT(sizeof(PageDirectory_t) == 8192, Invalid_PageDirectory_Alignment);

// Disable the atomic wrong alignment, as they are aligned and are sanitized
// by the static assert
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

/* MmVirtualCreatePageTable
 * Creates and initializes a new empty page-table */
CREATE_STRUCTURE_HELPER(PageTable_t, PageTable)

PageTable_t*
MmBootGetPageTable(
        _In_ PAGE_MASTER_LEVEL* masterTable,
        _In_ vaddr_t            address)
{
    PageTable_t* pageTable;

    pageTable = (PageTable_t*)(atomic_load(&masterTable->pTables[PAGE_DIRECTORY_INDEX(address)]) & PAGE_MASK);
    return pageTable;
}

static void 
MmVirtualFillPageTable(
        _In_ PageTable_t* pageTable,
        _In_ paddr_t      physicalBase,
        _In_ vaddr_t      virtualBase,
        _In_ unsigned int flags,
        _In_ size_t       length)
{
	uintptr_t address = physicalBase | flags;
	int       iStart  = PAGE_TABLE_INDEX(virtualBase);
	int       iEnd    = iStart + (int)(MIN(DIVUP(length, PAGE_SIZE), ENTRIES_PER_PAGE - iStart));

	// Iterate through pages and map them
	for (; iStart < iEnd; iStart++, address += PAGE_SIZE) {
        atomic_store(&pageTable->Pages[iStart], address);
	}
}

static void
MmVirtualMapMemoryRange(
        _In_ PageDirectory_t* pageDirectory,
        _In_ vaddr_t          addressStart,
        _In_ size_t           length,
        _In_ unsigned int     flags)
{
    int iStart = PAGE_DIRECTORY_INDEX(addressStart);
    int iEnd   = iStart + (int)(DIVUP(length, TABLE_SPACE_SIZE));
    int i;

    for (i = iStart; i < iEnd; i++) {
        if (pageDirectory->vTables[i] == 0) {
            uintptr_t physicalBase = (uintptr_t)MmVirtualCreatePageTable(&pageDirectory->vTables[i]);
            atomic_store(&pageDirectory->pTables[i], physicalBase | flags);
        }
    }
}

void
MmBootPrepareKernel(void)
{
    PageDirectory_t* pageDirectory;
	PageTable_t*     pageTable;
    size_t           bytesToMap;
    paddr_t          physicalBase;
    vaddr_t          virtualBase;
    oserr_t       osStatus;
    unsigned int     kernelPageFlags = PAGE_PRESENT | PAGE_WRITE;
	TRACE("MmBootPrepareKernel()");

    // Can we use global pages for kernel table?
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OS_EOK) {
        kernelPageFlags |= PAGE_GLOBAL;
    }

    osStatus = MachineAllocateBootMemory(
            sizeof(PageDirectory_t),
            &virtualBase,
            (paddr_t*)&pageDirectory
    );
    assert(osStatus == OS_EOK);

    memset((void*)pageDirectory, 0, sizeof(PageDirectory_t));
    g_kernelcr3 = (uintptr_t)pageDirectory;
    g_kernelpd  = virtualBase;

    // Due to how it works with multiple cpu's, we need to make sure all shared
    // tables already are mapped in the uppermost level of the page-directory

    // Allocate all neccessary memory before starting to identity map
    TRACE("MmBootPrepareKernel pre-mapping kernel memory from 0x%" PRIxIN " => 0x%" PRIxIN "",
          MEMORY_LOCATION_KERNEL, MEMORY_LOCATION_KERNEL + BYTES_PER_MB);
    MmVirtualMapMemoryRange(
            pageDirectory,
            MEMORY_LOCATION_KERNEL,
            BYTES_PER_MB,
            PAGE_PRESENT | PAGE_WRITE
    );

    TRACE("MmBootPrepareKernel pre-mapping kernel TLS memory from 0x%" PRIxIN " => 0x%" PRIxIN "",
          MEMORY_LOCATION_TLS_START, MEMORY_LOCATION_TLS_START + PAGE_SIZE);
    MmVirtualMapMemoryRange(
            pageDirectory,
            MEMORY_LOCATION_TLS_START,
            PAGE_SIZE,
            PAGE_PRESENT | PAGE_WRITE
    );

    TRACE("MmBootPrepareKernel pre-mapping shared memory from 0x%" PRIxIN " => 0x%" PRIxIN "",
          MEMORY_LOCATION_SHARED_START, MEMORY_LOCATION_SHARED_END);
    MmVirtualMapMemoryRange(
            pageDirectory,
            MEMORY_LOCATION_SHARED_START,
            MEMORY_LOCATION_SHARED_END - MEMORY_LOCATION_SHARED_START,
            PAGE_PRESENT | PAGE_WRITE
    );

    // Do the identity map process for the kernel mapping
    virtualBase  = MEMORY_LOCATION_KERNEL;
    physicalBase = MEMORY_LOCATION_KERNEL;
    bytesToMap   = BYTES_PER_MB;
    while (bytesToMap) {
        size_t length = MIN(bytesToMap, TABLE_SPACE_SIZE - (virtualBase % TABLE_SPACE_SIZE));
        TRACE("MmBootPrepareKernel identity mapping 0x%" PRIxIN " => 0x%" PRIxIN "",
              virtualBase, virtualBase + length);

        pageTable = MmBootGetPageTable(pageDirectory, virtualBase);
        MmVirtualFillPageTable(pageTable, physicalBase, virtualBase, kernelPageFlags, length);

        bytesToMap   -= length;
        physicalBase += length;
        virtualBase  += length;
    }
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
