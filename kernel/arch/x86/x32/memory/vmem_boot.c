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

extern void memory_set_paging(int enable);
extern uintptr_t AllocateBootMemory(size_t);
extern uintptr_t LastReservedAddress;

static uintptr_t PDBootPhysicalAddress = 0;

STATIC_ASSERT(sizeof(PageDirectory_t) == 8192, Invalid_PageDirectory_Alignment);

#define GET_TABLE_HELPER(MasterTable, Address) ((PageTable_t*)MasterTable->vTables[PAGE_DIRECTORY_INDEX(Address)])

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
	_In_ PageTable_t*       Table, 
	_In_ PhysicalAddress_t  PhysicalAddress, 
	_In_ VirtualAddress_t   VirtualAddress, 
	_In_ unsigned int            Flags,
	_In_ size_t             Length)
{
	uintptr_t pAddress = PhysicalAddress | Flags;
	int       PtStart  = PAGE_TABLE_INDEX(VirtualAddress);
	int       PtEnd    = MIN(PtStart + DIVUP(Length, PAGE_SIZE), ENTRIES_PER_PAGE);

	// Iterate through pages and map them
	for (; PtStart < PtEnd; PtStart++, pAddress += PAGE_SIZE) {
        atomic_store(&Table->Pages[PtStart], pAddress);
	}
}

static void
MmVirtualMapMemoryRange(
    _In_ PageDirectory_t* Directory,
	_In_ VirtualAddress_t AddressStart,
	_In_ size_t           Length,
	_In_ unsigned int          Flags)
{
    int PdStart = PAGE_DIRECTORY_INDEX(AddressStart);
    int PdEnd   = PdStart + DIVUP(Length, TABLE_SPACE_SIZE);
    int i;

    for (i = PdStart; i < PdEnd; i++) {
        if (Directory->vTables[i] == 0) {
            Directory->vTables[i] = (uintptr_t)MmVirtualCreatePageTable();
            atomic_store(&Directory->pTables[i], Directory->vTables[i] | Flags);
        }
    }
}

OsStatus_t
CreateKernelVirtualMemorySpace(void)
{
    PageDirectory_t*  Directory;
	PageTable_t*      Table;
    size_t            BytesToMap;
    PhysicalAddress_t PhysicalBase;
    VirtualAddress_t  VirtualBase;
    unsigned int           KernelPageFlags = PAGE_PRESENT | PAGE_WRITE;
	TRACE("[vmem] [boot_create]");

    // Can we use global pages for kernel table?
    if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
        KernelPageFlags |= PAGE_GLOBAL;
    }

    Directory = (PageDirectory_t*)AllocateBootMemory(sizeof(PageDirectory_t));
    memset((void*)Directory, 0, sizeof(PageDirectory_t));
    PDBootPhysicalAddress = (uintptr_t)Directory;

    // Due to how it works with multiple cpu's, we need to make sure all shared
    // tables already are mapped in the upper-most level of the page-directory
    TRACE("[vmem] [boot_create] pre-mapping kernel region from 0x%" PRIxIN " => 0x%" PRIxIN "", 
        0, MEMORY_LOCATION_KERNEL_END);
    
    // Allocate all neccessary memory before starting to identity map
    MmVirtualMapMemoryRange(Directory, 0, MEMORY_LOCATION_KERNEL_END, PAGE_PRESENT | PAGE_WRITE);
    
    // Get the number of reserved bytes - this address is the NEXT page available for
    // allocation, so subtract a page
    BytesToMap = READ_VOLATILE(LastReservedAddress) - PAGE_SIZE;

    // Do the identity map process for entire 2nd page - LastReservedAddress
    TRACE("[vmem] [boot_create] identity mapping 0x%" PRIxIN " => 0x%" PRIxIN "", 
        PAGE_SIZE, TABLE_SPACE_SIZE);
    MmVirtualFillPageTable(GET_TABLE_HELPER(Directory, (uint64_t)0), 
        PAGE_SIZE, PAGE_SIZE, KernelPageFlags, TABLE_SPACE_SIZE - PAGE_SIZE);
    VirtualBase  = TABLE_SPACE_SIZE;
    PhysicalBase = TABLE_SPACE_SIZE;
    BytesToMap  -= MIN(BytesToMap, TABLE_SPACE_SIZE);
    
    while (BytesToMap) {
        size_t Length = MIN(BytesToMap, TABLE_SPACE_SIZE);
        
        Table = GET_TABLE_HELPER(Directory, VirtualBase);
        TRACE("[vmem] [boot_create] identity mapping 0x%" PRIxIN " => 0x%" PRIxIN "", 
            VirtualBase, VirtualBase + Length);
        MmVirtualFillPageTable(Table, PhysicalBase, VirtualBase,
            KernelPageFlags, Length);
        
        BytesToMap   -= Length;
        PhysicalBase += Length;
        VirtualBase  += Length;
    }

    // Identity map the video framebuffer region to a new physical, and then free
    // all the physical mappings allocated for this
    if (GetMachine()->BootInformation.VbeMode) {
        BytesToMap   = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;
        PhysicalBase = VideoGetTerminal()->FrameBufferAddress;
        VirtualBase  = MEMORY_LOCATION_VIDEO;
        while (BytesToMap) {
            size_t Length = MIN(BytesToMap, TABLE_SPACE_SIZE);
            
            Table = GET_TABLE_HELPER(Directory, VirtualBase);
            MmVirtualFillPageTable(Table, PhysicalBase, VirtualBase,
                KernelPageFlags, Length);

            BytesToMap   -= Length;
            PhysicalBase += TABLE_SPACE_SIZE;
            VirtualBase  += TABLE_SPACE_SIZE;
        }

        // Update video address to the new
        VideoGetTerminal()->FrameBufferAddress = MEMORY_LOCATION_VIDEO;
    }
    return OsSuccess;
}

OsStatus_t
InitializeVirtualSpace(
    _In_ SystemMemorySpace_t* SystemMemorySpace)
{
	TRACE("[vmem] [initialize]");

    if (GetCurrentProcessorCore() == GetMachine()->Processor.Cores) {
        
        // Update the configuration data for the memory space
        SystemMemorySpace->Data[MEMORY_SPACE_CR3]       = PDBootPhysicalAddress;
        SystemMemorySpace->Data[MEMORY_SPACE_DIRECTORY] = PDBootPhysicalAddress;
        SystemMemorySpace->Data[MEMORY_SPACE_IOMAP]     = TssGetBootIoSpace();
        ArchMmuSwitchMemorySpace(SystemMemorySpace);
        memory_set_paging(1);
    }
    else {
        // Create a new page directory but copy all kernel mappings to the domain specific memory
        //iDirectory = (PageMasterTable_t*)kmalloc_p(sizeof(PageMasterTable_t), &iPhysical);
        NOTIMPLEMENTED("[vmem] [initialize] implement initialization of other-domain virtaul spaces");
    }
    return OsSuccess;
}


#if defined(__clang__)
#pragma clang diagnostic pop
#endif
