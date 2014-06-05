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
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Globals */
volatile uint32_t *memory_bitmap = NULL;
volatile uint32_t memory_bitmap_size = 0;
volatile uint32_t memory_blocks = 0;
volatile uint32_t memory_usedblocks = 0;
volatile uint32_t memory_size = 0;
spinlock_t memory_plock = 0;

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
	interrupt_status_t int_state;

	/* Get spinlock */
	int_state = interrupt_disable();
	spinlock_acquire(&memory_plock);

	/* Find time! */
	for (i = 0; i < 8; i++)
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
	interrupt_status_t int_state;

	/* Get spinlock */
	int_state = interrupt_disable();
	spinlock_acquire(&memory_plock);

	/* Find time! */
	for (i = 8; i < max; i++)
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

/* Initialises the physical memory bitmap */
void physmem_init(multiboot_info_t *bootinfo, uint32_t img_size)
{
	/* Step 1. Set location of memory bitmap at 2mb */
	mboot_mem_region_t *region = (mboot_mem_region_t*)bootinfo->MemoryMapAddr;
	uint32_t i;
	img_size = img_size;

	/* Get information from multiboot struct */
	memory_size = bootinfo->MemoryHigh;
	memory_size += bootinfo->MemoryLow; /* This is in kilobytes ... */
	memory_size *= 1024;

	/* Sanity, we need AT LEAST 4 mb to run! */
	assert((memory_size / 1024 / 1024) >= 2);

	/* Set storage variables */
	memory_bitmap = (uint32_t*)PHYS_MM_BITMAP_LOCATION;
	memory_blocks = memory_size / PAGE_SIZE;
	memory_usedblocks = memory_blocks;
	memory_bitmap_size = memory_blocks / 8; /* 8 blocks per byte, 32 per int */

	/* Set all memory in use */
	memset((void*)memory_bitmap, 0xF, memory_bitmap_size);

	/* Loop through memory regions from bootloader */
	for (i = 0; i < bootinfo->MemoryMapLength; i++)
	{
		printf("      > Memory Region %u: Address: 0x%x, Size 0x%x\n", 
			region->type, (uint32_t)region->address, (uint32_t)region->size);

		/* Available Region? */
		if (region->type == 1)
			memory_free_region((uint32_t)region->address, (uint32_t)region->size);

		/* Advance to next */
		region++;
	}

	/* Mark special regions as reserved */

	/* 0x4000 - 0x5000 || Used for memory region & Trampoline-code */
	memory_setbit(0x4000 / PAGE_SIZE);
	memory_usedblocks++;

	/* 0x90000 - 0x9F000 || Kernel Stack */
	memory_alloc_region(0x90000, 0xF000);

	/* 0x100000 - 0x140000 || Kernel Space */
	memory_alloc_region(PHYS_MM_KERNEL_LOCATION, PHYS_MM_KERNEL_RESERVED);

	/* 0x140000 - ?? || Bitmap Space */
	memory_alloc_region(PHYS_MM_BITMAP_LOCATION, memory_bitmap_size);

	printf("      > Bitmap size: %u Bytes\n", memory_bitmap_size);
	printf("      > Memory in use %u Bytes\n", memory_usedblocks * PAGE_SIZE);
}

void physmem_free_block(physaddr_t addr)
{
	/* Calculate Bit */
	int bit = (int32_t)(addr / PAGE_SIZE);
	interrupt_status_t int_state;

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
	memory_usedblocks--;
}

physaddr_t physmem_alloc_block(void)
{
	/* Get free bit */
	int bit = memory_get_free_bit_high();
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