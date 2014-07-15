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
* MollenOS x86-32 Physical Memory Manager
*/

/* Includes */
#include <arch.h>
#include <memory.h>
#include <multiboot.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Globals */
volatile addr_t *memory_bitmap = NULL;
volatile uint32_t memory_bitmap_size = 0;
volatile uint32_t memory_blocks = 0;
volatile uint32_t memory_usedblocks = 0;
volatile uint32_t memory_size = 0;
spinlock_t memory_plock = 0;

/* Reserved Regions */
sys_mappings_t reserved_mappings[32];

/* Helpers */
void memory_setbit(int bit)
{
	memory_bitmap[bit / 32] |= (1 << (bit % 32));
}

void memory_unsetbit(int bit)
{
	memory_bitmap[bit / 32] &= ~(1 << (bit % 32));
}

uint8_t memory_testbit(int bit)
{
	uint32_t block = memory_bitmap[bit / 32];
	uint32_t index = (1 << (bit % 32));

	if (block & index)
		return 1;
	else
		return 0;
}

/* Get a free bit in the bitmap 
 * at low memory < 1 mb */
int memory_get_free_bit_low(void)
{
	uint32_t i;
	int j;
	int rbit = -1;

	/* Get spinlock */
	interrupt_status_t int_state = interrupt_disable();
	spinlock_acquire(&memory_plock);

	/* Find time! */
	for (i = 1; i < 8; i++)
	{
		if (memory_bitmap[i] != 0xFFFFFFFF)
		{
			for (j = 0; j < 32; j++)
			{
				int bit = 1 << j;
				if (!(memory_bitmap[i] & bit))
				{
					rbit = (int)(i * 4 * 8 + j);
					break;
				}
			}
		}

		/* Check for break */
		if (rbit != -1)
			break;
	}

	/* Release Spinlock */
	spinlock_release(&memory_plock);
	interrupt_set_state(int_state);

	/* Return frame */
	return rbit;
}

/* Get a free bit in the bitmap */
/* at high memory > 1 mb */
int memory_get_free_bit_high(void)
{
	uint32_t i, max = memory_blocks;
	int j;
	int rbit = -1;

	/* Find time! */
	for (i = 8; i < max; i++)
	{
		if (memory_bitmap[i] != 0xFFFFFFFF)
		{
			for (j = 0; j < 32; j++)
			{
				int bit = 1 << j;

				/* Test it */
				if (!(memory_bitmap[i] & bit))
				{
					rbit = (int)(i * 4 * 8 + j);
					break;
				}
			}
		}

		/* Check for break */
		if (rbit != -1)
			break;
	}

	return rbit;
}

/* Frees a region of memory */
void memory_free_region(uint32_t base, size_t size)
{
	int align = (int32_t)(base / PAGE_SIZE);
	int blocks = (int32_t)(size / PAGE_SIZE);
	uint32_t i;

	for (i = base; blocks > 0; blocks--, i += PAGE_SIZE)
	{
		/* Free memory */
		memory_unsetbit(align++);

		if (memory_usedblocks != 0)
			memory_usedblocks--;
	}
}

/* Allocate a region of memory */
void memory_alloc_region(uint32_t base, size_t size)
{
	int align = (int32_t)(base / PAGE_SIZE);
	int blocks = (int32_t)(size / PAGE_SIZE);
	uint32_t i;

	for (i = base; (blocks + 1) > 0; blocks--, i += PAGE_SIZE)
	{
		/* Allocate memory */
		memory_setbit(align++);
		memory_usedblocks++;
	}
}

/* Mappings contains type and address already? */
int memory_reserved_contains(addr_t base, int type)
{
	uint32_t i;

	/* Find address, if it exists! */
	for (i = 0; i < 32; i++)
	{
		if (reserved_mappings[i].length == 0)
			continue;

		/* Does type match ? */
		if (reserved_mappings[i].type == type)
		{
			/* check if addr is matching */
			if (reserved_mappings[i].physical == base)
				return 1;
		}
	}

	return 0;
}

/* Initialises the physical memory bitmap */
void physmem_init(void *bootinfo, uint32_t img_size)
{
	/* Step 1. Set location of memory bitmap at 2mb */
	multiboot_info_t *mboot = (multiboot_info_t*)bootinfo;
	mboot_mem_region_t *region = (mboot_mem_region_t*)mboot->MemoryMapAddr;
	uint32_t i, j;
	
	_CRT_UNUSED(img_size);

	/* Get information from multiboot struct */
	memory_size = mboot->MemoryHigh;
	memory_size += mboot->MemoryLow; /* This is in kilobytes ... */
	memory_size *= 1024;

	/* Sanity, we need AT LEAST 4 mb to run! */
	assert((memory_size / 1024 / 1024) >= 2);

	/* Set storage variables */
	memory_bitmap = (addr_t*)PHYS_MM_BITMAP_LOCATION;
	memory_blocks = memory_size / PAGE_SIZE;
	memory_usedblocks = memory_blocks + 1;
	memory_bitmap_size = (memory_blocks + 1) / 8; /* 8 blocks per byte, 32 per int */

	if ((memory_blocks + 1) % 8)
		memory_bitmap_size++;

	/* Set all memory in use */
	memset((void*)memory_bitmap, 0xF, memory_bitmap_size);
	memset((void*)reserved_mappings, 0, sizeof(reserved_mappings));

	/* Reset Spinlock */
	spinlock_reset(&memory_plock);

	/* Let us make it possible to access 
	 * the first page of memory, but not through normal means */
	reserved_mappings[0].type = 2;
	reserved_mappings[0].physical = 0;
	reserved_mappings[0].virtual = 0;
	reserved_mappings[0].length = PAGE_SIZE;

	/* Loop through memory regions from bootloader */
	for (i = 0, j = 1; i < mboot->MemoryMapLength; i++)
	{
		if (!memory_reserved_contains((physaddr_t)region->address, (int)region->type))
		{
			/* Available Region? */
			if (region->type == 1)
				memory_free_region((physaddr_t)region->address, (size_t)region->size);

			/*printf("      > Memory Region %u: Address: 0x%x, Size 0x%x\n",
				region->type, (physaddr_t)region->address, (size_t)region->size); */

			reserved_mappings[j].type = region->type;
			reserved_mappings[j].physical = (physaddr_t)region->address;
			reserved_mappings[j].virtual = 0;
			reserved_mappings[j].length = (size_t)region->size;

			/* Advance */
			j++;
		}
		
		/* Advance to next */
		region++;
	}

	/* Mark special regions as reserved */

	/* 0x4000 - 0x6000 || Used for memory region & Trampoline-code */
	memory_setbit(0x4000 / PAGE_SIZE);
	memory_setbit(0x5000 / PAGE_SIZE);
	memory_usedblocks += 2;

	/* 0x90000 - 0x9F000 || Kernel Stack */
	memory_alloc_region(0x90000, 0xF000);

	/* 0x100000 - 0x180000 || Kernel Space */
	memory_alloc_region(PHYS_MM_KERNEL_LOCATION, PHYS_MM_KERNEL_RESERVED);

	/* 0x180000 - ?? || Bitmap Space */
	memory_alloc_region(PHYS_MM_BITMAP_LOCATION, memory_bitmap_size);

	printf("      > Bitmap size: %u Bytes\n", memory_bitmap_size);
	printf("      > Memory in use %u Bytes\n", memory_usedblocks * PAGE_SIZE);
}

void physmem_free_block(physaddr_t addr)
{
	/* Calculate Bit */
	int bit = (int32_t)(addr / PAGE_SIZE);
	interrupt_status_t int_state;

	/* Sanity */
	if (addr > memory_size
		|| addr < 0x200000)
		return;

	/* Get spinlock */
	int_state = interrupt_disable();
	spinlock_acquire(&memory_plock);

	/* Sanity */
	assert(memory_testbit(bit) != 0);

	/* Free it */
	memory_unsetbit(bit);

	/* Release Spinlock */
	spinlock_release(&memory_plock);
	interrupt_set_state(int_state);

	/* Statistics */
	if (memory_usedblocks != 0)
		memory_usedblocks--;
}

physaddr_t physmem_alloc_block(void)
{
	/* Get free bit */
	int bit;
	interrupt_status_t int_state;

	/* Get spinlock */
	int_state = interrupt_disable();
	spinlock_acquire(&memory_plock);

	bit = memory_get_free_bit_high();

	/* Sanity */
	assert(bit != -1);

	/* Set it */
	memory_setbit(bit);

	/* Release Spinlock */
	spinlock_release(&memory_plock);
	interrupt_set_state(int_state);

	/* Statistics */
	memory_usedblocks++;

	return (physaddr_t)(bit * PAGE_SIZE);
}

physaddr_t physmem_alloc_block_dma(void)
{
	/* Get free bit */
	int bit = memory_get_free_bit_low();
	interrupt_status_t int_state;

	/* Sanity */
	assert(bit != -1);

	/* Get spinlock */
	int_state = interrupt_disable();
	spinlock_acquire(&memory_plock);

	/* Set it */
	memory_setbit(bit);

	/* Release Spinlock */
	spinlock_release(&memory_plock);
	interrupt_set_state(int_state);

	/* Statistics */
	memory_usedblocks++;

	return (physaddr_t)(bit * PAGE_SIZE);
}

/* Get system region */
virtaddr_t memory_get_reserved_mapping(physaddr_t physical)
{
	uint32_t i;

	/* Find address, if it exists! */
	for (i = 0; i < 32; i++)
	{
		if (reserved_mappings[i].length != 0 && reserved_mappings[i].type != 1)
		{
			/* Get start and end */
			physaddr_t start = reserved_mappings[i].physical;
			physaddr_t end = reserved_mappings[i].physical + reserved_mappings[i].length - 1;

			/* Is it in range? :) */
			if (physical >= start
				&& physical <= end)
			{
				/* Yay, return virtual mapping! */
				return reserved_mappings[i].virtual + (physical - reserved_mappings[i].physical);
			}
		}
	}

	return 0;
}

/***************************
 * Physical Memory Manager
 * Testing Suite
 ***************************/
void physmem_test(void)
{
	/* Phase 1, basic allocation and freeing */
	physaddr_t i, addr1, addr2, addr3;

	printf("\nPhysical Memory Testing initiated...\n");
	printf("Initial Memory in use %u Bytes\n", memory_usedblocks * PAGE_SIZE);

	addr1 = physmem_alloc_block();
	addr2 = physmem_alloc_block();
	addr3 = physmem_alloc_block();

	printf("Alloc1: 0x%x, Alloc2: 0x%x, Alloc3: 0x%x\n", addr1, addr2, addr3);
	printf("Freeing addr2 and allocating a new, then freeing rest\n");
	physmem_free_block(addr2);
	addr2 = physmem_alloc_block();
	physmem_free_block(addr1);
	physmem_free_block(addr3);
	printf("Alloc2: 0x%x\n", addr2);
	physmem_free_block(addr2);

	/* Try allocating laaaarge */
	addr1 = physmem_alloc_block();
	printf("Large allocation start at: 0x%x\n", addr1);
	for (i = 0; i < 1023; i++)
		addr2 = physmem_alloc_block();
	printf("Large allocation end at: 0x%x\n", addr2);

	/* Freeing laaaarge! */
	printf("Freeing the large allocation!\n");
	for (i = addr1; i <= addr2; i += PAGE_SIZE)
		physmem_free_block(i);
	printf("Done, testing is concluded\n");
	printf("Final Memory in use %u Bytes\n", memory_usedblocks * PAGE_SIZE);
}