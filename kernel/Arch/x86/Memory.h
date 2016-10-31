/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS x86-32 Memory Definitions, Structures, Explanations
*/

#ifndef _X86_MEMORY_H_
#define _X86_MEMORY_H_

/* Includes 
 * - System */
#include <MollenOS.h>
#include <CriticalSection.h>

/**********************************/
/* Physical Memory Defs & Structs */
/**********************************/

/* This is the how many bits per register 
 * definition, used by the memory bitmap */
#define MEMORY_BITS					(sizeof(size_t) * 8)
#define MEMORY_LIMIT				~((Addr_t)0)
#define MEMORY_MASK_DEFAULT			~((Addr_t)0)

/* Memory Map Structure 
 * This is the structure passed to us by
 * the mBoot bootloader */
#pragma pack(push, 1)
typedef struct _MBootMemoryRegion
{
	/* The 64 bit address of where this 
	 * memory region starts */
	uint64_t	Address;

	/* The size of the this memory region 
	 * also 64 bit value */
	uint64_t	Size;

	/* The type of this memory region
	 * 1 => Available, 2 => ACPI, 3 => Reserved */
	uint32_t	Type;

	/* Null value, used for stuff i guess 
	 * and 8 bytes padding to reach 32 bytes 
	 * per entry */
	uint32_t	Nil;
	uint64_t	Padding;

} MBootMemoryRegion_t;
#pragma pack(pop)

/* System reserved memory mappings
 * this is to faster/safer map in system
 * memory like ACPI/device memory etc etc */
typedef struct _SysMemMapping
{
	/* Where this memory mapping starts
	 * in physical address space */
	PhysAddr_t PhysicalAddrStart;

	/* Where we have mapped this in 
	 * the virtual address space */
	VirtAddr_t VirtualAddrStart;

	/* Length of this memory mapping */
	size_t Length;

	/* Type. 2 - ACPI */
	int Type;

} SysMemMapping_t;

/* This is the physical memory manager initializor
* It reads the multiboot memory descriptor(s), initialies
* the bitmap and makes sure reserved regions are allocated */
_CRT_EXTERN void MmPhyiscalInit(void *BootInfo, MCoreBootDescriptor *Descriptor);

/* This is the primary function for allocating
* physical memory pages, this takes an argument
* <Mask> which determines where in memory the
* allocation is OK */
_CRT_EXTERN PhysAddr_t MmPhysicalAllocateBlock(Addr_t Mask);

/* This is the primary function for
* freeing physical pages, but NEVER free physical
* pages if they exist in someones mapping */
_CRT_EXTERN void MmPhysicalFreeBlock(PhysAddr_t Addr);

/* This function retrieves the virtual address 
 * of an mapped system mapping, this is to avoid
 * re-mapping and continous unmap of device memory 
 * Returns 0 if none exists */
_CRT_EXTERN VirtAddr_t MmPhyiscalGetSysMappingVirtual(PhysAddr_t PhysicalAddr);

/**********************************/
/* Virtual Memory Defs & Structs  */
/**********************************/

/* Structural Sizes */
#define PAGES_PER_TABLE			1024
#define TABLES_PER_PDIR			1024
#define TABLE_SPACE_SIZE		0x400000
#define DIRECTORY_SPACE_SIZE	0xFFFFFFFF

/* Shared PT/Page Definitions */
#define PAGE_PRESENT		0x1
#define PAGE_WRITE			0x2
#define PAGE_USER			0x4
#define PAGE_WRITETHROUGH	0x8
#define PAGE_CACHE_DISABLE	0x10
#define PAGE_ACCESSED		0x20

/* Page Table Definitions */
#define PAGETABLE_UNUSED	0x40
#define PAGETABLE_4MB		0x80
#define PAGETABLE_IGNORED	0x100

/* Page Definitions */
#define PAGE_DIRTY			0x40
#define PAGE_UNUSED			0x80
#define PAGE_GLOBAL			0x100

/* MollenOS PT/Page Definitions */
#define PAGE_SYSTEM_MAP		0x200
#define PAGE_INHERITED		0x400
#define PAGE_VIRTUAL		0x800

/* Masks */
#define PAGE_MASK			0xFFFFF000
#define ATTRIBUTE_MASK		0x00000FFF

/* Index's */
#define PAGE_DIRECTORY_INDEX(x) (((x) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)

/* Page Table */
typedef struct _PageTable
{
	/* Pages (Physical Bindings)
	 * Seen by MMU */
	uint32_t Pages[PAGES_PER_TABLE];

} PageTable_t;

/* Page Directory */
typedef struct _PageDirectory
{
	/* Page Tables (Physical Bindings)
	 * Seen by MMU */
	uint32_t pTables[TABLES_PER_PDIR];

	/* Page Tables (Virtual Mappings)
	 * Not seen by MMU */
	uint32_t vTables[TABLES_PER_PDIR];

	/* Lock */
	CriticalSection_t Lock;

} PageDirectory_t;



/* Virtual Memory */
_CRT_EXTERN void MmVirtualInit(void);
_CRT_EXTERN void MmVirtualMap(void *PageDirectory, PhysAddr_t PhysicalAddr, VirtAddr_t VirtualAddr, uint32_t Flags);
_CRT_EXTERN void MmVirtualUnmap(void *PageDirectory, VirtAddr_t VirtualAddr);
_CRT_EXTERN PhysAddr_t MmVirtualGetMapping(void *PageDirectory, VirtAddr_t VirtualAddr);

/* Hihi */
_CRT_EXTERN VirtAddr_t *MmReserveMemory(int Pages);
_CRT_EXTERN PageDirectory_t *MmVirtualGetCurrentDirectory(Cpu_t cpu);
_CRT_EXTERN void MmVirtualSwitchPageDirectory(Cpu_t cpu, PageDirectory_t* PageDirectory, PhysAddr_t Pdb);

/* Install paging for AP Cores */
_CRT_EXTERN void MmVirtualInstallPaging(Cpu_t cpu);

#endif // !_X86_MEMORY_H_
