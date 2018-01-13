/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS x86 Memory Definitions, Structures, Explanations
 */

#ifndef _X86_MEMORY_H_
#define _X86_MEMORY_H_

/* Includes 
 * - System */
#include <os/osdefs.h>
#include <multiboot.h>
#include <mutex.h>

/* Structural Sizes */
#define PAGES_PER_TABLE			1024
#define TABLES_PER_PDIR			1024
#define TABLE_SPACE_SIZE		0x400000
#define DIRECTORY_SPACE_SIZE	0xFFFFFFFF

/* Only allocate memory below 4mb */
#define MEMORY_INIT_MASK		0x3FFFFF

/* Shared PT/Page Definitions */
#define PAGE_PRESENT			0x1
#define PAGE_WRITE				0x2
#define PAGE_USER				0x4
#define PAGE_WRITETHROUGH		0x8
#define PAGE_CACHE_DISABLE		0x10
#define PAGE_ACCESSED			0x20

/* Page Table Definitions */
#define PAGETABLE_UNUSED		0x40
#define PAGETABLE_4MB			0x80
#define PAGETABLE_IGNORED		0x100

/* Page Definitions */
#define PAGE_DIRTY				0x40
#define PAGE_UNUSED				0x80
#define PAGE_GLOBAL				0x100

/* MollenOS PT/Page Definitions */
#define PAGE_SYSTEM_MAP			0x200
#define PAGE_INHERITED			0x400
#define PAGE_VIRTUAL			0x800

/* Masks */
#define PAGE_MASK				0xFFFFF000
#define ATTRIBUTE_MASK			0x00000FFF

/* Index's */
#define PAGE_DIRECTORY_INDEX(x) (((x) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)

/* Page Table Structure
 * Denotes how the paging structure is for the X86-32
 * platform, this is different from X86-64 */
PACKED_TYPESTRUCT(PageTable, {
	uint32_t				Pages[PAGES_PER_TABLE];
});

/* Page Directory Structure
 * Denotes how the paging structure is for the X86-32
 * platform, this is different from X86-64 */
PACKED_TYPESTRUCT(PageDirectory, {
	uint32_t				pTables[TABLES_PER_PDIR];	// Seen by MMU
	uint32_t				vTables[TABLES_PER_PDIR];	// Not seen by MMU
	Mutex_t					Lock;						// Not seen by MMU
});

/* Memory Map Structure 
 * This is the structure passed to us by
 * the mBoot bootloader */
PACKED_TYPESTRUCT(BIOSMemoryRegion, {
	uint64_t				Address;
	uint64_t				Size;
	uint32_t				Type;		//1 => Available, 2 => ACPI, 3 => Reserved
	uint32_t				Nil;
	uint64_t				Padding;
});

/* System reserved memory mappings
 * this is to faster/safer map in system
 * memory like ACPI/device memory etc etc */
PACKED_TYPESTRUCT(SystemMemoryMapping, {
	PhysicalAddress_t		pAddressStart;
	VirtualAddress_t		vAddressStart;
	size_t					Length;
	int						Type;	//Type. 2 - ACPI
});

/* MmPhyiscalInit
 * This is the physical memory manager initializor
 * It reads the multiboot memory descriptor(s), initialies
 * the bitmap and makes sure reserved regions are allocated */
KERNELAPI
OsStatus_t
KERNELABI
MmPhyiscalInit(
	_In_ Multiboot_t *BootInformation);

/* MmPhysicalQuery
 * Queries information about current block status */
KERNELAPI
OsStatus_t
KERNELABI
MmPhysicalQuery(
	_Out_Opt_ size_t *BlocksTotal,
	_Out_Opt_ size_t *BlocksAllocated);

/* MmPhysicalAllocateBlock
 * This is the primary function for allocating
 * physical memory pages, this takes an argument
 * <Mask> which determines where in memory the allocation is OK */
KERNELAPI
PhysicalAddress_t
KERNELABI
MmPhysicalAllocateBlock(
	_In_ uintptr_t Mask, 
	_In_ int Count);

/* MmPhysicalFreeBlock
 * This is the primary function for
 * freeing physical pages, but NEVER free physical
 * pages if they exist in someones mapping */
KERNELAPI
OsStatus_t
KERNELABI
MmPhysicalFreeBlock(
	_In_ PhysicalAddress_t Address);

/* MmPhyiscalGetSysMappingVirtual
 * This function retrieves the virtual address 
 * of an mapped system mapping, this is to avoid
 * re-mapping and continous unmap of device memory 
 * Returns 0 if none exists */
KERNELAPI
VirtualAddress_t
KERNELABI
MmPhyiscalGetSysMappingVirtual(
	_In_ PhysicalAddress_t PhysicalAddress);

/* MmVirtualInit
 * Initializes the virtual memory system and
 * installs default kernel mappings */
KERNELAPI
OsStatus_t
KERNELABI
MmVirtualInit(void);

/* MmVirtualSetFlags
 * Changes memory protection flags for the given virtual address */
KERNELAPI
OsStatus_t
KERNELABI
MmVirtualSetFlags(
	_In_ void*              PageDirectory, 
	_In_ VirtualAddress_t   vAddress, 
	_In_ Flags_t            Flags);

/* MmVirtualMap
 * Installs a new page-mapping in the given
 * page-directory. The type of mapping is controlled by
 * the Flags parameter. */
KERNELAPI
OsStatus_t
KERNELABI
MmVirtualMap(
	_In_ void *PageDirectory, 
	_In_ PhysicalAddress_t pAddress, 
	_In_ VirtualAddress_t vAddress, 
	_In_ Flags_t Flags);

/* MmVirtualUnmap
 * Unmaps a previous mapping from the given page-directory
 * the mapping must be present */
KERNELAPI
OsStatus_t
KERNELABI
MmVirtualUnmap(
	_In_ void *PageDirectory, 
	_In_ VirtualAddress_t Address);

/* MmVirtualGetMapping
 * Retrieves the physical address mapping of the
 * virtual memory address given - from the page directory 
 * that is given */
KERNELAPI
PhysicalAddress_t
KERNELABI
MmVirtualGetMapping(
	_In_ void *PageDirectory, 
	_In_ VirtualAddress_t Address);

/* MmReserveMemory
 * Reserves memory for system use - should be allocated
 * from a fixed memory region that won't interfere with
 * general usage */
KERNELAPI
VirtualAddress_t*
KERNELABI
MmReserveMemory(
	_In_ int Pages);

/* MmVirtualGetCurrentDirectory
 * Retrieves the current page-directory for the given cpu */
KERNELAPI
PageDirectory_t*
KERNELABI
MmVirtualGetCurrentDirectory(
	_In_ UUId_t Cpu);

/* MmVirtualSwitchPageDirectory
 * Switches page-directory for the current cpu
 * but the current cpu should be given as parameter
 * as well */
KERNELAPI
OsStatus_t
KERNELABI
MmVirtualSwitchPageDirectory(
	_In_ UUId_t Cpu, 
	_In_ PageDirectory_t* PageDirectory, 
	_In_ PhysicalAddress_t Pdb);

/* MmVirtualInstallPaging
 * Initializes paging for the given cpu id */
KERNELAPI
OsStatus_t
KERNELABI
MmVirtualInstallPaging(
	_In_ UUId_t Cpu);

#endif // !_X86_MEMORY_H_
