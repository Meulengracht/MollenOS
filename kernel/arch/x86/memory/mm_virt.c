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
* MollenOS x86-32 Virtual Memory Manager
*/

#include <arch.h>
#include <heap.h>
#include <video.h>
#include <memory.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Globals */
page_directory_t *kernel_directory = NULL;
page_directory_t *current_directories[64];
spinlock_t glb_vm_lock;
volatile addr_t glb_reserved_ptr = 0;

/* Externs */
extern volatile uint32_t num_cpus;
extern graphics_t gfx_info;
extern sys_mappings_t reserved_mappings[32];
extern void memory_set_paging(int enable);
extern void memory_load_cr3(addr_t pda);
extern void memory_reload_cr3(void);
extern void memory_invalidate_addr(addr_t pda);

/* Create a page-table */
page_table_t *memory_create_page_table(void)
{
	/* Allocate a page table */
	physaddr_t addr = physmem_alloc_block();
	page_table_t *ptable = (page_table_t*)addr;

	/* Sanity */
	assert((physaddr_t)ptable > 0);

	/* Zero it */
	memset((void*)ptable, 0, sizeof(page_table_t));

	return ptable;
}

/* Identity maps an address range */
void memory_fill_page_table(page_table_t *ptable, physaddr_t pstart, virtaddr_t vstart)
{
	/* Iterators */
	addr_t phys, virt;
	uint32_t i;

	/* Identity Map */
	for (i = PAGE_TABLE_INDEX(vstart), phys = pstart, virt = vstart; 
		i < 1024;
		i++, phys += PAGE_SIZE, virt += PAGE_SIZE)
	{
		/* Create Entry */
		uint32_t page = phys | PAGE_PRESENT | PAGE_WRITE;

		/* Set it at correct offset */
		ptable->Pages[PAGE_TABLE_INDEX(virt)] = page;
	}
}

/* Map physical range to virtual range */
void memory_map_phys_range_to_virt(page_directory_t* page_dir, 
	physaddr_t pstart, virtaddr_t vstart, addr_t size, uint32_t fill, uint32_t flags)
{
	uint32_t i, k;

	for (i = PAGE_DIRECTORY_INDEX(vstart), k = 0; 
		i < (PAGE_DIRECTORY_INDEX(vstart + size - 1) + 1); 
		i++, k++)
	{
		/* Initialize new page table */
		page_table_t *ptable = memory_create_page_table();

		/* Get addresses that match page table */
		uint32_t current_phys = pstart + (k * TABLE_SPACE_SIZE);
		uint32_t current_virt = vstart + (k * TABLE_SPACE_SIZE);

		/* Fill it */
		if (fill != 0)
			memory_fill_page_table(ptable, current_phys, current_virt);

		/* Install Table */
		page_dir->pTables[i] = (physaddr_t)ptable | PAGE_PRESENT | PAGE_WRITE | flags;
		page_dir->vTables[i] = (addr_t)ptable;
	}
}

/* Updates the current CPU current directory */
void memory_switch_directory(uint32_t cpu, page_directory_t* page_dir, physaddr_t pda)
{
	/* Sanity */
	assert(page_dir != NULL);

	/* Update Current */
	current_directories[cpu] = page_dir;

	/* Switch */
	memory_load_cr3(pda);

	/* Done */
	return;
}

/* Returns current memory directory */
page_directory_t *memory_get_current_pdir(cpu_t cpu)
{
	return current_directories[cpu];
}

/* Install paging for AP Cores */
void memory_install_paging(cpu_t cpu)
{
	/* Enable paging */
	memory_switch_directory(cpu, kernel_directory, (addr_t)kernel_directory);
	memory_set_paging(1);
}

/* Maps a virtual memory address to a physical
 * memory address in a given page-directory 
 * If page-directory is NULL, current directory
 * is used */
void memory_map(void *page_dir, physaddr_t phys, virtaddr_t virt, uint32_t flags)
{
	page_directory_t *pdir = (page_directory_t*)page_dir;
	page_table_t *ptable = NULL;
	
	/* Determine page directory */
	if (pdir == NULL)
	{
		/* Get CPU */
		pdir = (page_directory_t*)current_directories[get_cpu()];
	}

	/* Sanity */
	assert(pdir != NULL);

	/* Get spinlock */
	spinlock_acquire(&pdir->plock);

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(virt)] & PAGE_PRESENT))
	{
		/* No... Create it */
		addr_t phys_table = 0;
		page_table_t *ntable = NULL;

		/* Release spinlock */
		spinlock_release(&pdir->plock);

		/* Allocate new table */
		ntable = (page_table_t*)kmalloc_ap(PAGE_SIZE, &phys_table);

		/* Sanity */
		assert((addr_t)ntable > 0);

		/* Zero it */
		memset((void*)ntable, 0, sizeof(page_table_t));

		/* Get spinlock */
		spinlock_acquire(&pdir->plock);

		/* Install it */
		pdir->pTables[PAGE_DIRECTORY_INDEX(virt)] = phys_table | PAGE_PRESENT | PAGE_WRITE | flags;
		pdir->vTables[PAGE_DIRECTORY_INDEX(virt)] = (addr_t)ntable;

		/* Reload CR3 */
		if (page_dir == NULL)
			memory_reload_cr3();
	}

	/* Get it */
	ptable = (page_table_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(virt)];

	/* Now, lets map page! */
	assert(ptable->Pages[PAGE_TABLE_INDEX(virt)] == 0 
		&& "Dont remap pages without freeing :(" );

	/* Map it */
	ptable->Pages[PAGE_TABLE_INDEX(virt)] = (phys & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | flags;

	/* Release spinlock */
	spinlock_release(&pdir->plock);

	/* Invalidate Address */
	if (page_dir == NULL)
		memory_invalidate_addr(virt);
}

/* Unmaps a virtual memory address and frees the physical
* memory address in a given page-directory
* If page-directory is NULL, current directory
* is used */
void memory_unmap(void *page_dir, virtaddr_t virt)
{
	page_directory_t *pdir = (page_directory_t*)page_dir;
	page_table_t *ptable = NULL;
	physaddr_t phys = 0;

	/* Determine page directory */
	if (pdir == NULL)
	{
		/* Get CPU */
		pdir = (page_directory_t*)current_directories[get_cpu()];
	}

	/* Sanity */
	assert(pdir != NULL);

	/* Get spinlock */
	spinlock_acquire(&pdir->plock);

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(virt)] & PAGE_PRESENT))
	{
		/* No... What the fuck? */
		
		/* Release spinlock */
		spinlock_release(&pdir->plock);

		/* Return */
		return;
	}

	/* Get it */
	ptable = (page_table_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(virt)];

	/* Sanity */
	if (ptable->Pages[PAGE_TABLE_INDEX(virt)] == 0)
	{
		/* Release spinlock */
		spinlock_release(&pdir->plock);

		/* Return */
		return;
	}

	/* Do it */
	phys = ptable->Pages[PAGE_TABLE_INDEX(virt)];
	ptable->Pages[PAGE_TABLE_INDEX(virt)] = 0;

	/* Release memory */
	physmem_free_block(phys);

	/* Release spinlock */
	spinlock_release(&pdir->plock);

	/* Invalidate Address */
	if (page_dir == NULL)
		memory_invalidate_addr(virt);
}

/* Gets a physical memory address from a virtual
* memory address in a given page-directory
* If page-directory is NULL, current directory
* is used */
physaddr_t memory_getmap(void *page_dir, virtaddr_t virt)
{
	page_directory_t *pdir = (page_directory_t*)page_dir;
	page_table_t *ptable = NULL;
	physaddr_t phys = 0;

	/* Determine page directory */
	if (pdir == NULL)
	{
		/* Get CPU */
		pdir = (page_directory_t*)current_directories[get_cpu()];
	}

	/* Sanity */
	assert(pdir != NULL);

	/* Get spinlock */
	spinlock_acquire(&pdir->plock);

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(virt)] & PAGE_PRESENT))
	{
		/* No... */

		/* Release spinlock */
		spinlock_release(&pdir->plock);

		/* Return */
		return phys;
	}

	/* Get it */
	ptable = (page_table_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(virt)];

	/* Sanity */
	assert(ptable != NULL);

	/* Return mapping */
	phys = ptable->Pages[PAGE_TABLE_INDEX(virt)] & PAGE_MASK;

	/* Release spinlock */
	spinlock_release(&pdir->plock);

	/* Sanity */
	if (phys == 0)
		return 0;

	/* Done - Return with offset */
	return (phys + (virt & ATTRIBUTE_MASK));
}

/* Maps a virtual memory address to a physical
* memory address in a given page-directory
* If page-directory is NULL, current directory
* is used */
void memory_inital_map(physaddr_t phys, virtaddr_t virt)
{
	page_directory_t *pdir = kernel_directory;
	page_table_t *ptable = NULL;

	/* Does page table exist? */
	if (!(pdir->pTables[PAGE_DIRECTORY_INDEX(virt)] & PAGE_PRESENT))
	{
		/* No... Create it */
		page_table_t *ntable = memory_create_page_table();

		/* Zero it */
		memset((void*)ntable, 0, sizeof(page_table_t));

		/* Install it */
		pdir->pTables[PAGE_DIRECTORY_INDEX(virt)] = (addr_t)ntable | PAGE_PRESENT | PAGE_WRITE;
		pdir->vTables[PAGE_DIRECTORY_INDEX(virt)] = (addr_t)ntable;
	}

	/* Get it */
	ptable = (page_table_t*)pdir->vTables[PAGE_DIRECTORY_INDEX(virt)];

	/* Now, lets map page! */
	assert(ptable->Pages[PAGE_TABLE_INDEX(virt)] == 0
		&& "Dont remap pages without freeing :(");

	/* Map it */
	ptable->Pages[PAGE_TABLE_INDEX(virt)] = (phys & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE;
}

/* Map system memory */
virtaddr_t *memory_map_system_memory(physaddr_t physical, int pages)
{
	int i;
	cpu_t cpu;
	virtaddr_t ret = 0;

	/* Get cpu */
	cpu = get_cpu();

	/* Acquire Lock */
	spinlock_acquire(&glb_vm_lock);

	ret = glb_reserved_ptr;

	/* Map it */
	for (i = 0; i < pages; i++)
	{
		/* Call Map */
		if (!memory_getmap(memory_get_current_pdir(cpu), glb_reserved_ptr))
			memory_map(memory_get_current_pdir(cpu), physical + (i * PAGE_SIZE), glb_reserved_ptr, 0);

		/* Increase */
		glb_reserved_ptr += PAGE_SIZE;
	}

	/* Release */
	spinlock_release(&glb_vm_lock);

	return (virtaddr_t*)(ret + (physical & ATTRIBUTE_MASK));
}

/* Creates a page directory and loads it */
void virtmem_init(void)
{
	/* Variables we need */
	uint32_t i;
	page_table_t *itable;

	/* Allocate space */
	num_cpus = 0;
	glb_reserved_ptr = MEMORY_LOCATION_RESERVED;
	kernel_directory = (page_directory_t*)physmem_alloc_block();
	physmem_alloc_block(); physmem_alloc_block();
	itable = memory_create_page_table();

	/* Identity map only first 4 mB (THIS IS KERNEL ONLY) */
	memory_fill_page_table(itable, 0x1000, 0x1000);

	/* Clear out page_directory */
	memset((void*)kernel_directory, 0, sizeof(page_directory_t));

	/* Install it */
	kernel_directory->pTables[0] = (physaddr_t)itable | PAGE_PRESENT | PAGE_WRITE;
	kernel_directory->vTables[0] = (addr_t)itable;
	spinlock_reset(&kernel_directory->plock);
	spinlock_reset(&glb_vm_lock);

	/* Map Memory Regions */

	/* HEAP */
	printf("      > Mapping heap region to 0x%x\n", MEMORY_LOCATION_HEAP);
	memory_map_phys_range_to_virt(kernel_directory, 0, MEMORY_LOCATION_HEAP, 
		(MEMORY_LOCATION_HEAP_END - MEMORY_LOCATION_HEAP), 0, 0);

	/* SHARED MEMORY */
	printf("      > Mapping shared memory region to 0x%x\n", MEMORY_LOCATION_SHM);
	memory_map_phys_range_to_virt(kernel_directory, 0, MEMORY_LOCATION_SHM,
		(MEMORY_LOCATION_SHM_END - MEMORY_LOCATION_SHM), 0, PAGE_USER);

	/* VIDEO MEMORY (WITH FILL) */
	printf("      > Mapping video memory to 0x%x\n", MEMORY_LOCATION_VIDEO);
	memory_map_phys_range_to_virt(kernel_directory, gfx_info.VideoAddr, 
		MEMORY_LOCATION_VIDEO, (gfx_info.BytesPerScanLine * gfx_info.ResY), 1, PAGE_USER);

	/* Now, tricky, map reserved memory regions */

	/* Step 1. Install a pagetable at MEMORY_LOCATION_RESERVED */
	printf("      > Mapping reserved memory to 0x%x\n", MEMORY_LOCATION_RESERVED);

	/* Step 2. Map */
	for (i = 0; i < 32; i++)
	{
		if (reserved_mappings[i].length != 0 && reserved_mappings[i].type != 1)
		{
			/* Get page count */
			size_t page_length = reserved_mappings[i].length / PAGE_SIZE;
			uint32_t k;

			/* Round up */
			if (reserved_mappings[i].length % PAGE_SIZE)
				page_length++;

			/* Update entry */
			reserved_mappings[i].virtual = glb_reserved_ptr;

			/* Map it */
			for (k = 0; k < page_length; k++)
			{ 
				/* Call Map */
				memory_inital_map(((reserved_mappings[i].physical & PAGE_MASK) + (k * PAGE_SIZE)), glb_reserved_ptr);

				/* Increase */
				glb_reserved_ptr += PAGE_SIZE;
			}
		}
	}

	/* Modify Video Address */
	gfx_info.VideoAddr = MEMORY_LOCATION_VIDEO;

	/* Enable paging */
	memory_switch_directory(0, kernel_directory, (addr_t)kernel_directory);
	memory_set_paging(1);
}