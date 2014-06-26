/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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

/* Memory Layout in MollenOS Kernel Directory
* Address Start     Address End    Description
*
*  0x0              0xFFF          Unmapped to catch NULL pointers
*  0x1000           0x100000       Pre kernel, bootloader, stage2, SMP Init code
*  0x100000         0x400000       Kernel Space, PMM Bitmap, all pre-vmm allocs (3 MB)
*  0x400000         0x4000000      Kernel Heap (62 MB)
*  0x4000000        0x8000000	   VESA LFB Buffer (64 MB)
*  0x8000000        0x800F000      Kernel Stack (60 KB) 
*  0x9000000        0x30000000     Shared Memory (600~ MB)
*  0x30000000       0xA0000000     Unused Memory (2048 - 256 MB) 
*  0xA0000000       0xB0000000     Reserved Memory (256 MB)
*  0xB0000000       0xFFFFFFFF     Reserved Memory (Reserved - 1280 MB)
*/

/* Memory Layout in MollenOS User Directory
* Address Start     Address End    Description
*
*  0x0              0x100000       Unmapped
*  0x100000         0x400000       Kernel Space, PMM Bitmap, all pre-vmm allocs (3 MB)
*  0x400000         0x10000000     Kernel Heap (252 MB)
*  0x10010000       0x30000000     Shared Memory (512 MB)
*  0x30000000       0x50000000     Program Load Address (536 MB)
*  0x50000000       0x70000000     Program Libraries (512 MB)
*  0x70000000       0xFFEFFFFF     Program Heap (2047 MB)
*  0xFFF00000       0xFFFFFFF0     Program Stack (Max 1 MB)
*/


/* Includes */
#include <stdint.h>
#include <arch.h>

/**********************************/
/* Physical Memory Defs & Structs */
/**********************************/
#define PHYS_MM_KERNEL_LOCATION			0x100000

/* 256 Kb for kernel */
#define PHYS_MM_KERNEL_RESERVED			0x40000
#define PHYS_MM_BITMAP_LOCATION			(PHYS_MM_KERNEL_LOCATION + PHYS_MM_KERNEL_RESERVED)

/* Memory Map Structure */
#pragma pack(push, 1)
typedef struct mboot_mem_region
{
	uint64_t	address;
	uint64_t	size;
	uint32_t	type;
	uint32_t	nil;
} mboot_mem_region_t;
#pragma pack(pop)

/* System Reserved Mappings */
typedef struct sys_mappings
{
	/* Where it starts in physical */
	physaddr_t physical;

	/* Where we have mapped this virtual */
	virtaddr_t virtual;

	/* Length */
	size_t length;

	/* Type. 2 - ACPI */
	int type;

} sys_mappings_t;

/**********************************/
/* Virtual Memory Defs & Structs  */
/**********************************/

/* Structural Sizes */
#define PAGES_PER_TABLE		1024
#define TABLES_PER_PDIR		1024
#define TABLE_SPACE_SIZE	0x400000
#define DIRECTORY_SPACE_SIZE 0xFFFFFFFF

/* Page Definitions */
#define PAGE_PRESENT		0x1
#define PAGE_WRITE			0x2
#define PAGE_USER			0x4
#define PAGE_WRITETHROUGH	0x8
#define PAGE_CACHE_DISABLE	0x10
#define PAGE_ACCESSED		0x20
#define PAGE_DIRTY			0x40
#define PAGE_RESERVED		0x80
#define PAGE_GLOBAL			0x100
#define PAGE_SYSTEM_MAP		0x200

/* Masks */
#define PAGE_MASK			0xFFFFF000
#define ATTRIBUTE_MASK		0x00000FFF

/* Index's */
#define PAGE_DIRECTORY_INDEX(x) (((x) >> 22) & 0x3FF)
#define PAGE_TABLE_INDEX(x) (((x) >> 12) & 0x3FF)

/* Page Table */
typedef struct page_table
{
	/* Pages (Physical Bindings)
	* Seen by MMU */
	uint32_t Pages[PAGES_PER_TABLE];

} page_table_t;

/* Page Directory */
typedef struct page_directory
{
	/* Page Tables (Physical Bindings)
	 * Seen by MMU */
	uint32_t pTables[TABLES_PER_PDIR];

	/* Page Tables (Virtual Mappings)
	 * Not seen by MMU */
	uint32_t vTables[TABLES_PER_PDIR];

	/* Spinlock */
	spinlock_t plock;

} page_directory_t;


/* Hihi */
_CRT_EXTERN page_directory_t *memory_get_current_pdir(cpu_t cpu);
_CRT_EXTERN void memory_switch_directory(uint32_t cpu, page_directory_t* page_dir, physaddr_t pda);
_CRT_EXTERN virtaddr_t memory_get_reserved_mapping(physaddr_t physical);

/* Install paging for AP Cores */
_CRT_EXTERN void memory_install_paging(cpu_t cpu);

#endif // !_X86_MEMORY_H_
