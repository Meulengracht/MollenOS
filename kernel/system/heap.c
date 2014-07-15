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
* MollenOS Heap Manager (Dynamic Block System)
* DBS Heap Manager 
* Memory Nodes / Linked List / Lock Protectected (TODO REAPING )
*/

/* Heap Includes */
#include <arch.h>
#include <heap.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Globals */
heap_t *heap = NULL;
volatile addr_t heap_mem_start = MEMORY_LOCATION_HEAP + MEMORY_STATIC_OFFSET;
volatile addr_t heap_s_current = MEMORY_LOCATION_HEAP;
volatile addr_t heap_s_max = MEMORY_LOCATION_HEAP;
spinlock_t heap_plock = 0;

/* Heap Statistics */
volatile addr_t heap_bytes_allocated = 0;
volatile addr_t heap_num_allocs = 0;
volatile addr_t heap_num_frees = 0;
volatile addr_t heap_num_pages = 0;

/**************************************/
/******* Heap Helper Functions ********/
/**************************************/

addr_t *heap_salloc(size_t sz)
{
	interrupt_status_t int_state;
	addr_t *ret;

	/* Acquire Lock */
	int_state = interrupt_disable();
	spinlock_acquire(&heap_plock);

	/* Sanity */
	assert(sz > 0);

	/* Sanity */
	if ((heap_s_current + sz) >= heap_s_max)
	{
		/* Map? */
		if (!memory_getmap(NULL, heap_s_max))
			memory_map(NULL, physmem_alloc_block(), heap_s_max, 0);

		heap_s_max += PAGE_SIZE;
	}

	ret = (addr_t*)heap_s_current;
	memset((void*)heap_s_current, 0, sz);
	heap_s_current += sz;

	/* Release Lock */
	spinlock_release(&heap_plock);
	interrupt_set_state(int_state);

	return ret;
}

void heap_add_block_to_list(heap_block_t *block)
{
	heap_block_t *current_block, *previous_block;
	interrupt_status_t int_state;

	/* Sanity */
	assert(block != NULL);

	/* Acquire Lock */
	int_state = interrupt_disable();
	spinlock_acquire(&heap_plock);

	/* Loop To End */
	current_block = heap->blocks;
	previous_block = NULL;
	
	while (current_block)
	{
		previous_block = current_block;
		current_block = current_block->link;
	}

	/* Set as end */
	previous_block->link = block;

	/* Release Lock */
	spinlock_release(&heap_plock);
	interrupt_set_state(int_state);
}

void heap_remove_block_from_list(heap_block_t **previous_block, heap_block_t **node)
{
	interrupt_status_t int_state;

	/* Acquire Lock */
	int_state = interrupt_disable();
	spinlock_acquire(&heap_plock);

	/* Sanity */
	assert(previous_block != NULL && node != NULL);

	/* Remove */
	if ((*node)->link == NULL)
		(*previous_block)->link = NULL;
	else
		(*previous_block)->link = (*node)->link;

	/* Release Lock */
	spinlock_release(&heap_plock);
	interrupt_set_state(int_state);
}

addr_t heap_page_align_roundup(addr_t addr)
{
	addr_t res = addr;

	/* Page Align */
	if (res % PAGE_SIZE)
	{
		res &= PAGE_MASK;
		res += PAGE_SIZE;
	}

	return res;
}

void heap_print_stats(void)
{
	heap_block_t *current_block, *previous_block;
	uint32_t node_count = 0;
	size_t total_bytes_allocated = 0;
	size_t total_nodes_allocated = 0;

	/* Count Nodes */
	current_block = heap->blocks, previous_block = NULL;
	while (current_block)
	{
		heap_node_t *current_node = current_block->nodes, *previous_node = NULL;

		while (current_node)
		{
			/* Stats */
			total_nodes_allocated = (current_node->allocated == 0) ? 
				total_nodes_allocated : total_nodes_allocated + 1;
			total_bytes_allocated = (current_node->allocated == 0) ?
				total_bytes_allocated : (total_bytes_allocated + current_node->length);

			node_count++;

			/* Next Node */
			previous_node = current_node;
			current_node = current_node->link;
		}

		/* Next Block */
		previous_block = current_block;
		current_block = current_block->link;
	}

	/* Done, print */
	printf("Heap Stats:\n");
	printf("  -- Nodes Total: %u\n", node_count);
	printf("  -- Nodes Allocated: %u\n", total_nodes_allocated);
	printf("  -- Bytes Allocated: %u\n", total_bytes_allocated);
}

heap_block_t *heap_create_block(size_t node_size, int node_flags)
{
	/* Allocate a block & node */
	heap_block_t *block = (heap_block_t*)heap_salloc(sizeof(heap_block_t));
	heap_node_t *node = (heap_node_t*)heap_salloc(sizeof(heap_node_t));

	/* Set Members */
	block->addr_start = heap_mem_start;
	block->addr_end = (heap_mem_start + node_size - 1);
	block->bytes_free = node_size;
	block->flags = node_flags;
	block->link = NULL;
	block->nodes = node;

	/* Setup Spinlock */
	spinlock_reset(&block->plock);

	/* Setup node */
	node->addr = heap_mem_start;
	node->link = NULL;
	node->allocated = 0;
	node->length = node_size;

	/* Increament Address */
	heap_mem_start += node_size;

	return block;
}

/**************************************/
/********** Heap Expansion  ***********/
/**************************************/
void heap_expand(size_t size, int expand_type)
{
	heap_block_t *block;

	/* Normal expansion? */
	if (expand_type == ALLOCATION_NORMAL)
		block = heap_create_block(HEAP_NORMAL_BLOCK, BLOCK_NORMAL);
	else if (expand_type & ALLOCATION_ALIGNED)
		block = heap_create_block(HEAP_LARGE_BLOCK, BLOCK_LARGE);
	else
	{
		/* Page Align */
		size_t nsize = size;

		if (nsize & ATTRIBUTE_MASK)
		{
			nsize &= PAGE_MASK;
			nsize += PAGE_SIZE;
		}

		/* And allocate */
		block = heap_create_block(nsize, BLOCK_VERY_LARGE);
	}
	
	/* Add it to list */
	heap_add_block_to_list(block);
}

/**************************************/
/********** Heap Allocation ***********/
/**************************************/

/* Allocates <size> in a given <block> */
addr_t heap_allocate_in_block(heap_block_t *block, size_t size)
{
	heap_node_t *current_node = block->nodes, *previous_node = NULL;
	addr_t return_addr = 0;
	size_t pages = size / PAGE_SIZE;
	uint32_t i;

	/* Get spinlock */
	interrupt_status_t int_state = interrupt_disable();
	spinlock_acquire(&block->plock);

	/* Make sure we map enough pages */
	if (size % PAGE_SIZE)
		pages++;

	/* Standard block allocation algorithm */
	while (current_node)
	{
		/* Check if free and large enough */
		if (current_node->allocated == 0
			&& current_node->length >= size)
		{
			/* Allocate, two cases, either exact
			 * match in size or we make a new header */
			if (current_node->length == size
				|| block->flags & BLOCK_VERY_LARGE)
			{
				/* Easy peasy, set allocated and
				 * return */
				current_node->allocated = 1;
				return_addr = current_node->addr;
				block->bytes_free -= size;
				break;
			}
			else
			{
				/* Make new node */
				/* Insert it before this */
				heap_node_t *node = (heap_node_t*)heap_salloc(sizeof(heap_node_t));
				node->addr = current_node->addr;
				node->allocated = 1;
				node->length = size;
				node->link = current_node;
				return_addr = node->addr;

				/* Update current node stats */
				current_node->addr = node->addr + size;
				current_node->length -= size;

				/* Update previous */
				if (previous_node != NULL)
					previous_node->link = node;
				else
					block->nodes = node;

				break;
			}

		}

		/* Next node, search for a free */
		previous_node = current_node;
		current_node = current_node->link;
	}

	/* Release spinlock */
	spinlock_release(&block->plock);
	interrupt_set_state(int_state);

	if (return_addr != 0)
	{
		/* Do we step across page boundary? */
		if ((return_addr & PAGE_MASK)
			!= ((return_addr + size) & PAGE_MASK))
			pages++;

		/* Map */
		for (i = 0; i < pages; i++)
		{
			if (!memory_getmap(NULL, return_addr + (i * PAGE_SIZE)))
			{
				memory_map(NULL, physmem_alloc_block(), return_addr + (i * PAGE_SIZE), 0);
			}
		}
	}

	/* Return Address */
	return return_addr;
}

/* Finds a suitable block for allocation
 * and allocates in that block */
addr_t heap_allocate(size_t size, int flags)
{
	/* Find a block that match our 
	 * requirements */
	heap_block_t *current_block = heap->blocks, *previous_block = NULL;

	while (current_block)
	{
		/* Find a matching block */

		/* Check normal */
		if ((flags == ALLOCATION_NORMAL)
			&& (current_block->flags == BLOCK_NORMAL)
			&& (current_block->bytes_free >= size))
		{
			/* Try to make the allocation, THIS CAN 
			 * FAIL */
			addr_t res = heap_allocate_in_block(current_block, size);

			/* Check if failed */
			if (res != 0)
				return res;
		}

		/* Check aligned */
		if ((flags & ALLOCATION_ALIGNED)
			&& (current_block->flags & BLOCK_LARGE)
			&& (current_block->bytes_free >= size))
		{
			/* Var */
			addr_t res = 0;

			/* Page align allocation */
			if (size & ATTRIBUTE_MASK)
			{
				size &= PAGE_MASK;
				size += PAGE_SIZE;
			}

			/* Try to make the allocation, THIS CAN
			* FAIL */
			res = heap_allocate_in_block(current_block, size);

			/* Check if failed */
			if (res != 0)
				return res;
		}

		/* Check Special */
		if ((flags & ALLOCATION_SPECIAL)
			&& (current_block->flags & BLOCK_VERY_LARGE)
			&& (current_block->bytes_free >= size))
		{
			/* Try to make the allocation, THIS CAN
			* FAIL */
			addr_t res = heap_allocate_in_block(current_block, size);

			/* Check if failed */
			if (res != 0)
				return res;
		}

		/* Next Block */
		previous_block = current_block;
		current_block = current_block->link;
	}

	/* If we reach here, expand and research */
	heap_expand(size, flags);

	return heap_allocate(size, flags);
}

/* The real calls */

/* Page align & return physical */
void *kmalloc_ap(size_t sz, addr_t *p)
{
	/* Vars */
	addr_t addr;
	int flags = ALLOCATION_ALIGNED;

	/* Sanity */
	assert(sz != 0);

	/* Calculate some options */
	if (sz >= 0x4000)
	{
		/* Special Allocation */
		flags = ALLOCATION_SPECIAL;
	}

	/* Do the call */
	addr = heap_allocate(sz, flags);

	/* Sanity */
	assert(addr != 0);

	/* Now, get physical mapping */
	*p = memory_getmap(NULL, addr);

	/* Done */
	return (void*)addr;
}

/* Normal allocation, return physical */
void *kmalloc_p(size_t sz, addr_t *p)
{
	/* Vars */
	addr_t addr;
	int flags = ALLOCATION_NORMAL;

	/* Sanity */
	assert(sz != 0);

	/* Calculate some options */
	if (sz >= 0x500)
	{
		/* Do aligned allocation */
		flags = ALLOCATION_ALIGNED;
	}

	if (sz >= 0x4000)
	{
		/* Special Allocation */
		flags = ALLOCATION_SPECIAL;
	}

	/* Do the call */
	addr = heap_allocate(sz, flags);

	/* Sanity */
	assert(addr != 0);

	/* Get physical mapping */
	*p = memory_getmap(NULL, addr);

	/* Done */
	return (void*)addr;
}

/* Page aligned allocation */
void *kmalloc_a(size_t sz)
{
	/* Vars */
	addr_t addr;
	int flags = ALLOCATION_ALIGNED;

	/* Sanity */
	assert(sz != 0);

	/* Calculate some options */
	if (sz >= 0x4000)
	{
		/* Special Allocation */
		flags = ALLOCATION_SPECIAL;
	}

	/* Do the call */
	addr = heap_allocate(sz, flags);

	/* Sanity */
	assert(addr != 0);

	/* Done */
	return (void*)addr;
}

/* Normal allocation */
void *kmalloc(size_t sz)
{
	/* Vars */
	addr_t addr;
	int flags = ALLOCATION_NORMAL;

	/* Sanity */
	assert(sz != 0);

	/* Calculate some options */
	if (sz >= 0x500)
	{
		/* Do aligned allocation */
		flags = ALLOCATION_ALIGNED;
	}
		
	if (sz >= 0x4000)
	{
		/* Special Allocation */
		flags = ALLOCATION_SPECIAL;
	}

	/* Do the call */
	addr = heap_allocate(sz, flags);

	/* Sanity */
	assert(addr != 0);

	/* Done */
	return (void*)addr;
}

/**************************************/
/*********** Heap Freeing *************/
/**************************************/

/* Free in node */
void heap_free_in_node(heap_block_t *block, addr_t addr)
{
	/* Vars */
	heap_node_t *current_node = block->nodes, *previous_node = NULL;

	/* Get spinlock */
	interrupt_status_t int_state = interrupt_disable();
	spinlock_acquire(&block->plock);

	/* Standard block freeing algorithm */
	while (current_node)
	{
		/* Calculate end and start */
		addr_t start = current_node->addr;
		addr_t end = current_node->addr + current_node->length - 1;
		uint8_t merged = 0;

		/* Check if address is a part of this node */
		if (start <= addr && end >= addr)
		{
			/* Well, well, well. */
			current_node->allocated = 0;
			block->bytes_free += current_node->length;

			/* CHECK IF WE CAN MERGE!! */

			/* Can we merge with previous? */
			if (previous_node != NULL
				&& previous_node->allocated == 0)
			{
				/* Add this length to previous */
				previous_node->length += current_node->length;

				/* Remove this link (TODO SAVE HEADERS) */
				previous_node->link = current_node->link;
				merged = 1;
			}

			/* Merge with next in list? */

			/* Two cases, we already merged, or we did not */
			if (merged)
			{
				/* This link is dead, and now is previous */
				if (previous_node->link != NULL
					&& previous_node->link->allocated == 0)
				{
					/* Add length */
					previous_node->length += previous_node->link->length;

					/* Remove the link (TODO SAVE HEADERS) */
					current_node = previous_node->link->link;
					previous_node->link = current_node;
				}
			}
			else
			{
				/* We did not merget with previous, current is still alive! */
				if (current_node->link != NULL
					&& current_node->link->allocated == 0)
				{
					/* Merge time! */
					current_node->length += current_node->link->length;

					/* Remove then link (TODO SAVE HEADERS) */
					previous_node = current_node->link->link;
					current_node->link = previous_node;
				}
			}

			break;
		}

		/* Next node, search for the allocated block */
		previous_node = current_node;
		current_node = current_node->link;
	}

	/* Release spinlock */
	spinlock_release(&block->plock);
	interrupt_set_state(int_state);
}

/* Finds the appropriate block
 * that should contain our node */
void heap_free(addr_t addr)
{
	/* Find a block that match our
	* address */
	heap_block_t *current_block = heap->blocks, *previous_block = NULL;

	while (current_block)
	{
		/* Correct block? */
		if (!(current_block->addr_start > addr
			|| current_block->addr_end < addr))
		{
			heap_free_in_node(current_block, addr);
			return;
		}
		
		/* Next Block */
		previous_block = current_block;
		current_block = current_block->link;
	}
}

/* Free call */
void kfree(void *p)
{
	/* Sanity */
	assert(p != NULL);

	/* Free */
	heap_free((addr_t)p);

	/* Set NULL */
	p = NULL;
}

/**************************************/
/******** Heap Initialization *********/
/**************************************/

void heap_init(void)
{
	/* Vars */
	heap_block_t *block_normal, *block_special;

	/* Initiate the global spinlock */
	spinlock_reset(&heap_plock);

	/* Allocate the heap */
	heap = (heap_t*)heap_salloc(sizeof(heap_t));

	/* Create a normal node */
	block_normal = heap_create_block(HEAP_NORMAL_BLOCK, BLOCK_NORMAL);
	block_special = heap_create_block(HEAP_LARGE_BLOCK, BLOCK_LARGE);

	/* Insert them */
	block_normal->link = block_special;
	heap->blocks = block_normal;

	/* Heap is now ready to use! */
}

/**************************************/
/******** Heap Testing Suite  *********/
/**************************************/
void heap_test(void)
{
	/* Do some debugging */
	uint32_t phys1 = 0, phys2 = 0, i = 0;
	void *res1, *res2, *res3, *res4, *res5, *res6;

	heap_print_stats();

	printf(" >> Performing small allocs & frees (a few)\n");
	res1 = kmalloc(0x30);
	res2 = kmalloc(0x50);
	res3 = kmalloc(0x130);
	res4 = kmalloc(0x180);
	res5 = kmalloc(0x600);
	res6 = kmalloc(0x3000);

	heap_print_stats();

	printf(" Alloc1 (0x30): 0x%x, Alloc2 (0x50): 0x%x, Alloc3 (0x130): 0x%x, Alloc4 (0x180): 0x%x\n",
		(uint32_t)res1, (uint32_t)res2, (uint32_t)res3, (uint32_t)res4);
	printf(" Alloc5 (0x600): 0x%x, Alloc6 (0x3000): 0x%x\n", (uint32_t)res5, (uint32_t)res6);

	printf(" Freeing Alloc5, 2 & 3...\n");
	kfree(res5);
	kfree(res2);
	kfree(res3);

	heap_print_stats();

	printf(" Re-allocing 5, 2 & 3\n");
	res2 = kmalloc(0x90);
	res3 = kmalloc(0x20);
	res5 = kmalloc(0x320);

	printf(" Alloc2 (0x90): 0x%x, Alloc3 (0x20): 0x%x, Alloc5 (0x320): 0x%x\n", (uint32_t)res2, (uint32_t)res3, (uint32_t)res5);
	printf(" Freeing all...\n");

	kfree(res1);
	kfree(res2);
	kfree(res3);
	kfree(res4);
	kfree(res5);
	kfree(res6);

	heap_print_stats();

	printf(" Making special allocations (aligned & aligned /w phys)\n");
	res1 = kmalloc_a(0x30);
	res2 = kmalloc_a(0x210);
	res3 = kmalloc_a(0x900);
	res4 = kmalloc_a(0x4500);

	res5 = kmalloc_ap(0x32, &phys1);
	res6 = kmalloc_ap(0x1000, &phys2);

	printf(" Alloc1 (0x30): 0x%x, Alloc2 (0x210): 0x%x, Alloc3 (0x900): 0x%x, Alloc4 (0x4500): 0x%x\n",
		(uint32_t)res1, (uint32_t)res2, (uint32_t)res3, (uint32_t)res4);
	printf(" Alloc5 (0x32): 0x%x, Alloc6 (0x1000): 0x%x\n", (uint32_t)res5, (uint32_t)res6);
	printf(" Alloc5 Physical: 0x%x, Alloc6 Physical: 0x%x\n", phys1, phys2);

	printf(" Freeing all...\n");
	kfree(res1);
	kfree(res2);
	kfree(res3);
	kfree(res4);
	kfree(res5);
	kfree(res6);

	heap_print_stats();

	printf(" >> Performing allocations (150)\n");
	
	for (i = 0; i < 50; i++)
	{
		kmalloc(0x50);
		kmalloc(0x180);
		kmalloc(0x3000);
	}

	heap_print_stats();
	
	printf(" >> Performing allocs & frees (150)\n");
	for (i = 0; i < 50; i++)
	{
		void* r1 = kmalloc(0x30);
		void* r2 = kmalloc(0x130);
		void* r3 = kmalloc(0x600);

		kfree(r1);
		kfree(r2);
		kfree(r3);
	}
	
	//Done
	printf(" >> Memory benchmarks are done!\n");

}