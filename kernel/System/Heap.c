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
#include <Arch.h>
#include <Heap.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Globals */
Heap_t *Heap = NULL;
volatile Addr_t HeapMemStartData = MEMORY_LOCATION_HEAP + MEMORY_STATIC_OFFSET;
volatile Addr_t HeapMemHeaderCurrent = MEMORY_LOCATION_HEAP;
volatile Addr_t HeapMemHeaderMax = MEMORY_LOCATION_HEAP;
Mutex_t HeapLock;

/* Heap Statistics */
volatile Addr_t HeapBytesAllocated = 0;
volatile Addr_t HeapNumAllocs = 0;
volatile Addr_t HeapNumFrees = 0;
volatile Addr_t HeapNumPages = 0;

/**************************************/
/******* Heap Helper Functions ********/
/**************************************/
Addr_t *HeapSAllocator(size_t Size)
{
	Addr_t *ret;

	/* Sanity */
	assert(Size > 0);

	/* Sanity */
	if ((HeapMemHeaderCurrent + Size) >= HeapMemHeaderMax)
	{
		/* Sanity */
		if ((HeapMemHeaderMax + PAGE_SIZE) >= (MEMORY_LOCATION_HEAP + MEMORY_STATIC_OFFSET))
		{
			printf("HeapMgr: RAN OUT OF MEMORY\n");
			for (;;);
		}

		/* Map */
		MmVirtualMap(NULL, MmPhysicalAllocateBlock(), HeapMemHeaderMax, 0);
		memset((void*)HeapMemHeaderMax, 0, PAGE_SIZE);
		HeapMemHeaderMax += PAGE_SIZE;
	}

	ret = (Addr_t*)HeapMemHeaderCurrent;
	HeapMemHeaderCurrent += Size;

	return ret;
}

/* Appends block to end of block list */
void HeapAppendBlockToList(HeapBlock_t *Block)
{
	HeapBlock_t *current_block;

	/* Sanity */
	assert(Block != NULL);

	/* Loop To End */
	current_block = Heap->Blocks;

	while (current_block->Link)
		current_block = current_block->Link;

	/* Set as end */
	current_block->Link = Block;
}

/* Rounds up an address to page */
Addr_t HeapAlignPageWithRoundup(Addr_t Address)
{
	Addr_t res = Address;

	/* Page Align */
	if (res % PAGE_SIZE)
	{
		res &= PAGE_MASK;
		res += PAGE_SIZE;
	}

	return res;
}

/* Prints stats of heap */
void HeapPrintStats(void)
{
	HeapBlock_t *current_block, *previous_block;
	uint32_t node_count = 0;
	size_t total_bytes_allocated = 0;
	size_t total_nodes_allocated = 0;

	/* Count Nodes */
	current_block = Heap->Blocks, previous_block = NULL;
	while (current_block)
	{
		HeapNode_t *current_node = current_block->Nodes, *previous_node = NULL;

		while (current_node)
		{
			/* Stats */
			total_nodes_allocated = (current_node->Allocated == 0) ? 
				total_nodes_allocated : total_nodes_allocated + 1;
			total_bytes_allocated = (current_node->Allocated == 0) ?
				total_bytes_allocated : (total_bytes_allocated + current_node->Length);

			node_count++;

			/* Next Node */
			previous_node = current_node;
			current_node = current_node->Link;
		}

		/* Next Block */
		previous_block = current_block;
		current_block = current_block->Link;
	}

	/* Done, print */
	printf("Heap Stats:\n");
	printf("  -- Nodes Total: %u\n", node_count);
	printf("  -- Nodes Allocated: %u\n", total_nodes_allocated);
	printf("  -- Bytes Allocated: %u\n", total_bytes_allocated);
}

/* Helper to allocate and create a block */
HeapBlock_t *HeapCreateBlock(size_t node_size, int node_flags)
{
	/* Allocate a block & node */
	HeapBlock_t *block = (HeapBlock_t*)HeapSAllocator(sizeof(HeapBlock_t));
	HeapNode_t *node = (HeapNode_t*)HeapSAllocator(sizeof(HeapNode_t));

	/* Set Members */
	block->AddressStart = HeapMemStartData;
	block->AddressEnd = (HeapMemStartData + node_size - 1);
	block->BytesFree = node_size;
	block->Flags = node_flags;
	block->Link = NULL;
	block->Nodes = node;

	/* Setup Mutex */
	MutexConstruct(&block->Mutex);

	/* Setup node */
	node->Address = HeapMemStartData;
	node->Link = NULL;
	node->Allocated = 0;
	node->Length = node_size;

	/* Increament Address */
	HeapMemStartData += node_size;

	return block;
}

/**************************************/
/********** Heap Expansion  ***********/
/**************************************/
void HeapExpand(size_t Size, int ExpandType)
{
	HeapBlock_t *block;

	/* Normal expansion? */
	if (ExpandType == ALLOCATION_NORMAL)
		block = HeapCreateBlock(HEAP_NORMAL_BLOCK, BLOCK_NORMAL);
	else if (ExpandType & ALLOCATION_ALIGNED)
		block = HeapCreateBlock(HEAP_LARGE_BLOCK, BLOCK_LARGE);
	else
	{
		/* Page Align */
		size_t nsize = HeapAlignPageWithRoundup(Size);

		/* And allocate */
		block = HeapCreateBlock(nsize, BLOCK_VERY_LARGE);
	}
	
	/* Add it to list */
	HeapAppendBlockToList(block);
}

/**************************************/
/********** Heap Allocation ***********/
/**************************************/

/* Allocates <size> in a given <block> */
Addr_t HeapAllocateSizeInBlock(HeapBlock_t *Block, size_t Size)
{
	HeapNode_t *current_node = Block->Nodes, *previous_node = NULL;
	Addr_t return_addr = 0;
	size_t pages = Size / PAGE_SIZE;
	uint32_t i;

	/* Make sure we map enough pages */
	if (Size % PAGE_SIZE)
		pages++;

	/* Standard block allocation algorithm */
	while (current_node)
	{
		/* Check if free and large enough */
		if (current_node->Allocated == 0
			&& current_node->Length >= Size)
		{
			/* Allocate, two cases, either exact
			 * match in size or we make a new header */
			if (current_node->Length == Size
				|| Block->Flags & BLOCK_VERY_LARGE)
			{
				/* Easy peasy, set allocated and
				 * return */
				current_node->Allocated = 1;
				return_addr = current_node->Address;
				Block->BytesFree -= Size;
				break;
			}
			else
			{
				/* Make new node */
				/* Insert it before this */
				HeapNode_t *node = (HeapNode_t*)HeapSAllocator(sizeof(HeapNode_t));
				node->Address = current_node->Address;
				node->Allocated = 1;
				node->Length = Size;
				node->Link = current_node;
				return_addr = node->Address;

				/* Update current node stats */
				current_node->Address = node->Address + Size;
				current_node->Length -= Size;

				/* Update previous */
				if (previous_node != NULL)
					previous_node->Link = node;
				else
					Block->Nodes = node;

				break;
			}

		}

		/* Next node, search for a free */
		previous_node = current_node;
		current_node = current_node->Link;
	}

	if (return_addr != 0)
	{
		/* Do we step across page boundary? */
		if ((return_addr & PAGE_MASK)
			!= ((return_addr + Size) & PAGE_MASK))
			pages++;

		/* Map */
		for (i = 0; i < pages; i++)
		{
			if (!MmVirtualGetMapping(NULL, return_addr + (i * PAGE_SIZE)))
				MmVirtualMap(NULL, MmPhysicalAllocateBlock(), return_addr + (i * PAGE_SIZE), 0);
		}
	}

	/* Return Address */
	return return_addr;
}

/* Finds a suitable block for allocation
 * and allocates in that block */
Addr_t HeapAllocate(size_t Size, int Flags)
{
	/* Find a block that match our 
	 * requirements */
	HeapBlock_t *current_block = Heap->Blocks, *previous_block = NULL;

	while (current_block)
	{
		/* Find a matching block */

		/* Check normal */
		if ((Flags == ALLOCATION_NORMAL)
			&& (current_block->Flags == BLOCK_NORMAL)
			&& (current_block->BytesFree >= Size))
		{
			/* Try to make the allocation, THIS CAN 
			 * FAIL */
			Addr_t res = HeapAllocateSizeInBlock(current_block, Size);

			/* Check if failed */
			if (res != 0)
				return res;
		}

		/* Check aligned */
		if ((Flags == ALLOCATION_ALIGNED)
			&& (current_block->Flags & BLOCK_LARGE)
			&& (current_block->BytesFree >= Size))
		{
			/* Var */
			Addr_t res = 0;
			size_t a_size = Size;

			/* Page align allocation */
			if (a_size & ATTRIBUTE_MASK)
			{
				a_size &= PAGE_MASK;
				a_size += PAGE_SIZE;
			}

			/* Try to make the allocation, THIS CAN
			* FAIL */
			res = HeapAllocateSizeInBlock(current_block, a_size);

			/* Check if failed */
			if (res != 0)
				return res;
		}

		/* Check Special */
		if ((Flags == ALLOCATION_SPECIAL)
			&& (current_block->Flags & BLOCK_VERY_LARGE)
			&& (current_block->BytesFree >= Size))
		{
			/* Try to make the allocation, THIS CAN
			* FAIL */
			Addr_t res = HeapAllocateSizeInBlock(current_block, Size);

			/* Check if failed */
			if (res != 0)
				return res;
		}

		/* Next Block */
		previous_block = current_block;
		current_block = current_block->Link;
	}

	/* If we reach here, expand and research */
	HeapExpand(Size, Flags);

	return HeapAllocate(Size, Flags);
}

/* The real calls */

/* Page align & return physical */
void *kmalloc_ap(size_t sz, Addr_t *p)
{
	/* Vars */
	Addr_t addr;
	int flags = ALLOCATION_ALIGNED;

	/* Sanity */
	assert(sz != 0);

	/* Lock */
	MutexLock(&HeapLock);

	/* Calculate some options */
	if (sz >= 0x3000)
	{
		/* Special Allocation */
		flags = ALLOCATION_SPECIAL;
	}

	/* Do the call */
	addr = HeapAllocate(sz, flags);

	/* Release */
	MutexUnlock(&HeapLock);

	/* Sanity */
	assert(addr != 0);

	/* Now, get physical mapping */
	*p = MmVirtualGetMapping(NULL, addr);

	/* Done */
	return (void*)addr;
}

/* Normal allocation, return physical */
void *kmalloc_p(size_t sz, Addr_t *p)
{
	/* Vars */
	Addr_t addr;
	int flags = ALLOCATION_NORMAL;

	/* Sanity */
	assert(sz != 0);

	/* Lock */
	MutexLock(&HeapLock);

	/* Calculate some options */
	if (sz >= 0x500)
	{
		/* Do aligned allocation */
		flags = ALLOCATION_ALIGNED;
	}

	if (sz >= 0x3000)
	{
		/* Special Allocation */
		flags = ALLOCATION_SPECIAL;
	}

	/* Do the call */
	addr = HeapAllocate(sz, flags);

	/* Release */
	MutexUnlock(&HeapLock);

	/* Sanity */
	assert(addr != 0);

	/* Get physical mapping */
	*p = MmVirtualGetMapping(NULL, addr);

	/* Done */
	return (void*)addr;
}

/* Page aligned allocation */
void *kmalloc_a(size_t sz)
{
	/* Vars */
	Addr_t addr;
	int flags = ALLOCATION_ALIGNED;

	/* Sanity */
	assert(sz > 0);

	/* Lock */
	MutexLock(&HeapLock);

	/* Calculate some options */
	if (sz >= 0x3000)
	{
		/* Special Allocation */
		flags = ALLOCATION_SPECIAL;
	}

	/* Do the call */
	addr = HeapAllocate(sz, flags);

	/* Release */
	MutexUnlock(&HeapLock);

	/* Sanity */
	assert(addr != 0);

	/* Done */
	return (void*)addr;
}

/* Normal allocation */
void *kmalloc(size_t sz)
{
	/* Vars */
	Addr_t addr;
	int flags = ALLOCATION_NORMAL;

	/* Sanity */
	assert(sz > 0);

	/* Lock */
	MutexLock(&HeapLock);

	/* Calculate some options */
	if (sz >= 0x500)
	{
		/* Do aligned allocation */
		flags = ALLOCATION_ALIGNED;
	}
	
	if (sz >= 0x3000)
	{
		/* Special Allocation */
		flags = ALLOCATION_SPECIAL;
	}

	/* Do the call */
	addr = HeapAllocate(sz, flags);

	/* Release */
	MutexUnlock(&HeapLock);

	/* Sanity */
	assert(addr != 0);

	/* Done */
	return (void*)addr;
}

/**************************************/
/*********** Heap Freeing *************/
/**************************************/
/* Free in node */
void HeapFreeAddressInNode(HeapBlock_t *Block, Addr_t Address)
{
	/* Vars */
	HeapNode_t *current_node = Block->Nodes, *previous_node = NULL;

	/* Standard block freeing algorithm */
	while (current_node)
	{
		/* Calculate end and start */
		Addr_t start = current_node->Address;
		Addr_t end = current_node->Address + current_node->Length - 1;
		uint8_t merged = 0;

		/* Check if address is a part of this node */
		if (start <= Address && end >= Address)
		{
			/* Well, well, well. */
			current_node->Allocated = 0;
			Block->BytesFree += current_node->Length;

			/* CHECK IF WE CAN MERGE!! */

			/* Can we merge with previous? */
			if (previous_node != NULL
				&& previous_node->Allocated == 0)
			{
				/* Add this length to previous */
				previous_node->Length += current_node->Length;

				/* Remove this link (TODO SAVE HEADERS) */
				previous_node->Link = current_node->Link;
				merged = 1;
			}

			/* Merge with next in list? */

			/* Two cases, we already merged, or we did not */
			if (merged)
			{
				/* This link is dead, and now is previous */
				if (previous_node->Link != NULL
					&& previous_node->Link->Allocated == 0)
				{
					/* Add length */
					previous_node->Length += previous_node->Link->Length;

					/* Remove the link (TODO SAVE HEADERS) */
					current_node = previous_node->Link->Link;
					previous_node->Link = current_node;
				}
			}
			else
			{
				/* We did not merget with previous, current is still alive! */
				if (current_node->Link != NULL
					&& current_node->Link->Allocated == 0)
				{
					/* Merge time! */
					current_node->Length += current_node->Link->Length;

					/* Remove then link (TODO SAVE HEADERS) */
					previous_node = current_node->Link->Link;
					current_node->Link = previous_node;
				}
			}

			break;
		}

		/* Next node, search for the allocated block */
		previous_node = current_node;
		current_node = current_node->Link;
	}
}

/* Finds the appropriate block
 * that should contain our node */
void HeapFree(Addr_t addr)
{
	/* Find a block that match our
	* address */
	HeapBlock_t *current_block = Heap->Blocks, *previous_block = NULL;

	while (current_block)
	{
		/* Correct block? */
		if (!(current_block->AddressStart > addr
			|| current_block->AddressEnd < addr))
		{
			HeapFreeAddressInNode(current_block, addr);
			return;
		}
		
		/* Next Block */
		previous_block = current_block;
		current_block = current_block->Link;
	}
}

/* Free call */
void kfree(void *p)
{
	/* Sanity */
	assert(p != NULL); 

	/* Lock */
	MutexLock(&HeapLock);

	/* Free */
	HeapFree((Addr_t)p);

	/* Release */
	MutexUnlock(&HeapLock);

	/* Set NULL */
	p = NULL;
}

/**************************************/
/******** Heap Initialization *********/
/**************************************/
void HeapInit(void)
{
	/* Vars */
	HeapBlock_t *block_normal, *block_special;

	/* Just to be safe */
	HeapMemStartData = MEMORY_LOCATION_HEAP + MEMORY_STATIC_OFFSET;
	HeapMemHeaderCurrent = MEMORY_LOCATION_HEAP;
	HeapMemHeaderMax = MEMORY_LOCATION_HEAP;

	/* Initiate the global mutex */
	MutexConstruct(&HeapLock);

	/* Allocate the heap */
	Heap = (Heap_t*)HeapSAllocator(sizeof(Heap_t));

	/* Create a normal node */
	block_normal = HeapCreateBlock(HEAP_NORMAL_BLOCK, BLOCK_NORMAL);
	block_special = HeapCreateBlock(HEAP_LARGE_BLOCK, BLOCK_LARGE);

	/* Insert them */
	block_normal->Link = block_special;
	Heap->Blocks = block_normal;

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

	HeapPrintStats();

	printf(" >> Performing small allocs & frees (a few)\n");
	res1 = kmalloc(0x30);
	res2 = kmalloc(0x50);
	res3 = kmalloc(0x130);
	res4 = kmalloc(0x180);
	res5 = kmalloc(0x600);
	res6 = kmalloc(0x3000);

	HeapPrintStats();

	printf(" Alloc1 (0x30): 0x%x, Alloc2 (0x50): 0x%x, Alloc3 (0x130): 0x%x, Alloc4 (0x180): 0x%x\n",
		(uint32_t)res1, (uint32_t)res2, (uint32_t)res3, (uint32_t)res4);
	printf(" Alloc5 (0x600): 0x%x, Alloc6 (0x3000): 0x%x\n", (uint32_t)res5, (uint32_t)res6);

	printf(" Freeing Alloc5, 2 & 3...\n");
	kfree(res5);
	kfree(res2);
	kfree(res3);

	HeapPrintStats();

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

	HeapPrintStats();

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

	HeapPrintStats();

	printf(" >> Performing allocations (150)\n");
	
	for (i = 0; i < 50; i++)
	{
		kmalloc(0x50);
		kmalloc(0x180);
		kmalloc(0x3000);
	}

	HeapPrintStats();
	
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