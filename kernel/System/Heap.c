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

/* CLib */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* Globals */
Heap_t *Heap = NULL;

/* Recyclers */
HeapBlock_t *GlbHeapBlockRecycler = NULL;
HeapNode_t *GlbHeapNodeRecycler = NULL;

/* Memory Globals */
volatile Addr_t HeapMemStartData = MEMORY_LOCATION_HEAP + MEMORY_STATIC_OFFSET;
volatile Addr_t HeapMemHeaderCurrent = MEMORY_LOCATION_HEAP;
volatile Addr_t HeapMemHeaderMax = MEMORY_LOCATION_HEAP;
CriticalSection_t HeapLock;

/* Heap Statistics */
Addr_t HeapBytesAllocated = 0;
Addr_t HeapNumAllocs = 0;
Addr_t HeapNumFrees = 0;
Addr_t HeapNumPages = 0;

/**************************************/
/******* Heap Helper Functions ********/
/**************************************/
Addr_t *HeapSAllocator(size_t Size)
{
	Addr_t *RetAddr;

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

	RetAddr = (Addr_t*)HeapMemHeaderCurrent;
	HeapMemHeaderCurrent += Size;

	return RetAddr;
}

/* Appends block to end of block list */
void HeapAppendBlockToList(HeapBlock_t *Block)
{
	HeapBlock_t *CurrBlock;

	/* Sanity */
	assert(Block != NULL);

	/* Loop To End */
	CurrBlock = Heap->Blocks;

	while (CurrBlock->Link)
		CurrBlock = CurrBlock->Link;

	/* Set as end */
	CurrBlock->Link = Block;
}

/* Rounds up an address to page */
Addr_t HeapAlignPageWithRoundup(Addr_t Address)
{
	Addr_t Aligned = Address;

	/* Page Align */
	if (Aligned % PAGE_SIZE)
	{
		Aligned &= PAGE_MASK;
		Aligned += PAGE_SIZE;
	}

	return Aligned;
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
HeapBlock_t *HeapCreateBlock(size_t Size, int Flags)
{
	/* Allocate a block & node */
	HeapBlock_t *hBlock = NULL;
	HeapNode_t *hNode = NULL;

	/* Sanity Recycler */
	if (GlbHeapBlockRecycler != NULL)
	{
		/* Pop */
		hBlock = GlbHeapBlockRecycler;
		GlbHeapBlockRecycler = GlbHeapBlockRecycler->Link;
	}
	else
		hBlock = (HeapBlock_t*)HeapSAllocator(sizeof(HeapBlock_t));

	/* Sanity Recycler */
	if (GlbHeapNodeRecycler != NULL)
	{
		/* Pop */
		hNode = GlbHeapNodeRecycler;
		GlbHeapNodeRecycler = GlbHeapNodeRecycler->Link;
	}
	else
		hNode = (HeapNode_t*)HeapSAllocator(sizeof(HeapNode_t));

	/* Set Members */
	hBlock->AddressStart = HeapMemStartData;
	hBlock->AddressEnd = (HeapMemStartData + Size - 1);
	hBlock->BytesFree = Size;
	hBlock->Flags = Flags;
	hBlock->Link = NULL;
	hBlock->Nodes = hNode;

	/* Setup node */
	hNode->Address = HeapMemStartData;
	hNode->Link = NULL;
	hNode->Allocated = 0;
	hNode->Length = Size;

	/* Increament Address */
	HeapMemStartData += Size;

	/* Done! */
	return hBlock;
}

/**************************************/
/********** Heap Expansion  ***********/
/**************************************/
void HeapExpand(size_t Size, int ExpandType)
{
	HeapBlock_t *hBlock;

	/* Normal expansion? */
	if (ExpandType == ALLOCATION_NORMAL)
		hBlock = HeapCreateBlock(HEAP_NORMAL_BLOCK, BLOCK_NORMAL);
	else if (ExpandType & ALLOCATION_ALIGNED)
		hBlock = HeapCreateBlock(HEAP_LARGE_BLOCK, BLOCK_LARGE);
	else
	{
		/* Page Align */
		size_t nsize = HeapAlignPageWithRoundup(Size);

		/* And allocate */
		hBlock = HeapCreateBlock(nsize, BLOCK_VERY_LARGE);
	}
	
	/* Add it to list */
	HeapAppendBlockToList(hBlock);
}

/**************************************/
/********** Heap Allocation ***********/
/**************************************/

/* Allocates <size> in a given <block> */
Addr_t HeapAllocateSizeInBlock(HeapBlock_t *Block, size_t Size)
{
	HeapNode_t *CurrNode = Block->Nodes, *PrevNode = NULL;
	Addr_t RetAddr = 0;

	/* Standard block allocation algorithm */
	while (CurrNode)
	{
		/* Check if free and large enough */
		if (CurrNode->Allocated == 0
			&& CurrNode->Length >= Size)
		{
			/* Allocate, two cases, either exact
			 * match in size or we make a new header */
			if (CurrNode->Length == Size
				|| Block->Flags & BLOCK_VERY_LARGE)
			{
				/* Easy peasy, set allocated and
				 * return */
				CurrNode->Allocated = 1;
				RetAddr = CurrNode->Address;
				Block->BytesFree -= Size;
				break;
			}
			else
			{
				/* Make new node */
				/* Insert it before this */
				HeapNode_t *hNode = NULL;

				/* Sanity Recycler */
				if (GlbHeapNodeRecycler != NULL)
				{
					/* Pop */
					hNode = GlbHeapNodeRecycler;
					GlbHeapNodeRecycler = GlbHeapNodeRecycler->Link;
				}
				else
					hNode = (HeapNode_t*)HeapSAllocator(sizeof(HeapNode_t));

				/* Fill */
				hNode->Address = CurrNode->Address;
				hNode->Allocated = 1;
				hNode->Length = Size;
				hNode->Link = CurrNode;
				RetAddr = hNode->Address;

				/* Update current node stats */
				CurrNode->Address = hNode->Address + Size;
				CurrNode->Length -= Size;

				/* Update previous */
				if (PrevNode != NULL)
					PrevNode->Link = hNode;
				else
					Block->Nodes = hNode;

				break;
			}

		}

		/* Next node, search for a free */
		PrevNode = CurrNode;
		CurrNode = CurrNode->Link;
	}

	/* Return Address */
	return RetAddr;
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

	/* Recursive Call */
	return HeapAllocate(Size, Flags);
}

/* Map Pages */
void HeapSanityPages(Addr_t Address, size_t Size)
{
	/* Vars */
	size_t Pages = Size / PAGE_SIZE;
	uint32_t i;

	/* Sanity */
	if (Size % PAGE_SIZE)
		Pages++;

	/* Do we step across page boundary? */
	if ((Address & PAGE_MASK)
		!= ((Address + Size - 1) & PAGE_MASK))
		Pages++;

	/* Map */
	for (i = 0; i < Pages; i++)
	{
		if (!MmVirtualGetMapping(NULL, Address + (i * PAGE_SIZE)))
			MmVirtualMap(NULL, MmPhysicalAllocateBlock(), Address + (i * PAGE_SIZE), 0);
	}
}

/* The real calls */

/* Page align & return physical */
void *kmalloc_ap(size_t sz, Addr_t *p)
{
	/* Vars */
	Addr_t RetAddr;
	int Flags = ALLOCATION_ALIGNED;

	/* Sanity */
	assert(sz != 0);

	/* Special Allocation? */
	if (sz >= 0x3000)
		Flags = ALLOCATION_SPECIAL;

	/* Lock */
	CriticalSectionEnter(&HeapLock);

	/* Do the call */
	RetAddr = HeapAllocate(sz, Flags);

	/* Release */
	CriticalSectionLeave(&HeapLock);

	/* Sanity */
	assert(RetAddr != 0);

	/* Sanity Pages */
	HeapSanityPages(RetAddr, sz);

	/* Now, get physical mapping */
	*p = MmVirtualGetMapping(NULL, RetAddr);

	/* Done */
	return (void*)RetAddr;
}

/* Normal allocation, return physical */
void *kmalloc_p(size_t sz, Addr_t *p)
{
	/* Vars */
	Addr_t RetAddr;
	int Flags = ALLOCATION_NORMAL;

	/* Sanity */
	assert(sz != 0);

	/* Do aligned allocation´? */
	if (sz >= 0x500)
		Flags = ALLOCATION_ALIGNED;

	/* Special Allocation? */
	if (sz >= 0x3000)
		Flags = ALLOCATION_SPECIAL;

	/* Lock */
	CriticalSectionEnter(&HeapLock);

	/* Do the call */
	RetAddr = HeapAllocate(sz, Flags);

	/* Release */
	CriticalSectionLeave(&HeapLock);

	/* Sanity */
	assert(RetAddr != 0);

	/* Sanity Pages */
	HeapSanityPages(RetAddr, sz);

	/* Get physical mapping */
	*p = MmVirtualGetMapping(NULL, RetAddr);

	/* Done */
	return (void*)RetAddr;
}

/* Page aligned allocation */
void *kmalloc_a(size_t sz)
{
	/* Vars */
	Addr_t RetAddr;
	int Flags = ALLOCATION_ALIGNED;

	/* Sanity */
	assert(sz > 0);

	/* Special Allocation? */
	if (sz >= 0x3000)
		Flags = ALLOCATION_SPECIAL;

	/* Lock */
	CriticalSectionEnter(&HeapLock);

	/* Do the call */
	RetAddr = HeapAllocate(sz, Flags);

	/* Release */
	CriticalSectionLeave(&HeapLock);

	/* Sanity */
	assert(RetAddr != 0);

	/* Sanity Pages */
	HeapSanityPages(RetAddr, sz);

	/* Done */
	return (void*)RetAddr;
}

/* Normal allocation */
void *kmalloc(size_t sz)
{
	/* Vars */
	Addr_t RetAddr;
	int Flags = ALLOCATION_NORMAL;

	/* Sanity */
	assert(sz > 0);

	/* Do aligned allocation´? */
	if (sz >= 0x500)
		Flags = ALLOCATION_ALIGNED;

	/* Special Allocation? */
	if (sz >= 0x3000)
		Flags = ALLOCATION_SPECIAL;

	/* Lock */
	CriticalSectionEnter(&HeapLock);

	/* Do the call */
	RetAddr = HeapAllocate(sz, Flags);

	/* Release */
	CriticalSectionLeave(&HeapLock);

	/* Sanity */
	assert(RetAddr != 0);

	/* Sanity Pages */
	HeapSanityPages(RetAddr, sz);

	/* Done */
	return (void*)RetAddr;
}

/**************************************/
/*********** Heap Freeing *************/
/**************************************/
void HeapFreeAddressInNode(HeapBlock_t *Block, Addr_t Address)
{
	/* Vars */
	HeapNode_t *CurrNode = Block->Nodes, *PrevNode = NULL;

	/* Standard block freeing algorithm */
	while (CurrNode != NULL)
	{
		/* Calculate end and start */
		Addr_t aStart = CurrNode->Address;
		Addr_t aEnd = CurrNode->Address + CurrNode->Length - 1;

		/* Check if address is a part of this node */
		if (aStart <= Address && aEnd >= Address)
		{
			/* Well, well, well. */
			CurrNode->Allocated = 0;
			Block->BytesFree += CurrNode->Length;

			/* CHECK IF WE CAN MERGE!! */
			if (PrevNode == NULL
				&& CurrNode->Link == NULL)
				return;

			/* Can we merge with previous? */
			if (PrevNode != NULL
				&& PrevNode->Allocated == 0)
			{
				/* Add this length to previous */
				PrevNode->Length += CurrNode->Length;

				/* Remove this link */
				PrevNode->Link = CurrNode->Link;
				
				/* Recycle us */
				CurrNode->Link = NULL;
				CurrNode->Address = 0;
				CurrNode->Length = 0;

				/* Sanity */
				if (GlbHeapNodeRecycler == NULL)
					GlbHeapNodeRecycler = CurrNode;
				else
				{
					/* Front-us */
					CurrNode->Link = GlbHeapNodeRecycler;
					GlbHeapNodeRecycler = CurrNode;
				}
			}
			else if (CurrNode->Link != NULL
				&& CurrNode->Link->Allocated == 0)
			{
				/* We are root, move our data */
				CurrNode->Link->Address = CurrNode->Address;
				CurrNode->Link->Length += CurrNode->Length;

				/* Discard us */
				if (PrevNode == NULL)
					Block->Nodes = CurrNode->Link;
				else
					PrevNode->Link = CurrNode->Link;

				/* Recycle us */
				CurrNode->Link = NULL;
				CurrNode->Address = 0;
				CurrNode->Length = 0;

				/* Sanity */
				if (GlbHeapNodeRecycler == NULL)
					GlbHeapNodeRecycler = CurrNode;
				else
				{
					/* Front-us */
					CurrNode->Link = GlbHeapNodeRecycler;
					GlbHeapNodeRecycler = CurrNode;
				}
			}

			/* Done */
			break;
		}

		/* Next node, search for the allocated block */
		PrevNode = CurrNode;
		CurrNode = CurrNode->Link;
	}
}

/* Finds the appropriate block
 * that should contain our node */
void HeapFree(Addr_t Addr)
{
	/* Find a block that match our
	* address */
	HeapBlock_t *CurrBlock = Heap->Blocks;

	/* Try to locate the block */
	while (CurrBlock)
	{
		/* Correct block? */
		if (CurrBlock->AddressStart <= Addr
			&& CurrBlock->AddressEnd > Addr)
		{
			/* Yay, free */
			HeapFreeAddressInNode(CurrBlock, Addr);

			/* Done! */
			return;
		}
		
		/* Next Block */
		CurrBlock = CurrBlock->Link;
	}
}

/* Free call */
void kfree(void *p)
{
	/* Sanity */
	assert(p != NULL); 

	/* Lock */
	CriticalSectionEnter(&HeapLock);

	/* Free */
	HeapFree((Addr_t)p);

	/* Release */
	CriticalSectionLeave(&HeapLock);

	/* Set NULL */
	p = NULL;
}

/**************************************/
/******** Heap Initialization *********/
/**************************************/
void HeapInit(void)
{
	/* Vars */
	HeapBlock_t *NormBlock, *SpecBlock;

	/* Just to be safe */
	HeapMemStartData = MEMORY_LOCATION_HEAP + MEMORY_STATIC_OFFSET;
	HeapMemHeaderCurrent = MEMORY_LOCATION_HEAP;
	HeapMemHeaderMax = MEMORY_LOCATION_HEAP;

	/* Set null */
	GlbHeapBlockRecycler = NULL;
	GlbHeapNodeRecycler = NULL;

	/* Initiate the global spinlock */
	CriticalSectionConstruct(&HeapLock);

	/* Allocate the heap */
	Heap = (Heap_t*)HeapSAllocator(sizeof(Heap_t));

	/* Create a normal node */
	NormBlock = HeapCreateBlock(HEAP_NORMAL_BLOCK, BLOCK_NORMAL);
	SpecBlock = HeapCreateBlock(HEAP_LARGE_BLOCK, BLOCK_LARGE);

	/* Insert them */
	NormBlock->Link = SpecBlock;
	Heap->Blocks = NormBlock;

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