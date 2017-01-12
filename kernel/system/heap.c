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
 * MollenOS Heap Manager (Dynamic Buddy Block System)
 * DBBS Heap Manager
 * Version History:
 * - Version 1.1: The heap allocator is being upgraded yet again
 *                to involve better error checking, boundary checking
 *                and involve a buddy-allocator system
 *
 * - Version 1.0: Upgraded the linked list allocator to
 *                be a dynamic block system allocator and to support
 *                memory masks, aligned allocations etc
 *
 * - Version 0.1: Linked list memory allocator, simple but it worked
 */

/* Includes 
 * - System */
#include <Arch.h>
#include <Heap.h>
#include <Log.h>

/* Includes 
 * - C-Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* These are internal allocation flags
 * and get applied based on size for optimization */
#define ALLOCATION_PAGEALIGN		0x10000000
#define ALLOCATION_BIG				0x20000000

/* Helper to define whether or not an allocation is normal
 * or modified by allocation flags as alignment/size */
#define ALLOCISNORMAL(x)			((x & (ALLOCATION_PAGEALIGN | ALLOCATION_BIG)) == 0)
#define ALLOCISNOTBIG(x)			((x & ALLOCATION_BIG) == 0)
#define ALLOCISPAGE(x)				(((x & ALLOCATION_PAGEALIGN) != 0) && ALLOCISNOTBIG(x))

/* Globals */
const char *GlbKernelUnknown = "Unknown";
Heap_t GlbKernelHeap = { 0 };

/* The heap-structure allocation function
 * this allocates heap memory in the reserved
 * header space */
Addr_t *HeapSAllocator(Heap_t *Heap, size_t Size)
{
	/* The return address */
	Addr_t *RetAddr = NULL;

	/* Sanitize the size 
	 * if we ask for invalid size something has
	 * gone VERY wrong */
	assert(Size > 0);

	/* Sanitize our max-addr
	 * We might need to map in new memory */
	if ((Heap->MemHeaderCurrent + Size) >= Heap->MemHeaderMax)
	{
		/* Sanitize end of header memory */
		if ((Heap->MemHeaderMax + PAGE_SIZE) >= 
			(Heap->HeapBase + MEMORY_STATIC_OFFSET))
		{
			/* Debug */
			LogFatal("HEAP", "OUT OF MEM, HeaderMax: 0x%x, HeaderCurrent 0x%x", 
				Heap->MemHeaderMax, Heap->MemHeaderCurrent);
			HeapPrintStats(Heap);

			/* Daaamn 
			 * we don't have to assert here, we can simply
			 * just return null, another assert will catch it */
			return RetAddr;
		}

		/* Map in the new memory */
		AddressSpaceMap(AddressSpaceGetCurrent(), Heap->MemHeaderMax, PAGE_SIZE, 
			MEMORY_MASK_DEFAULT, (Heap->IsUser == 1) ? ADDRESS_SPACE_FLAG_APPLICATION : 0);

		/* Reset the memory */
		memset((void*)Heap->MemHeaderMax, 0, PAGE_SIZE);

		/* Increase limit */
		Heap->MemHeaderMax += PAGE_SIZE;
	}

	/* Get next new memory */
	RetAddr = (Addr_t*)Heap->MemHeaderCurrent;
	Heap->MemHeaderCurrent += Size;

	/* Done! */
	return RetAddr;
}

/* Helper function that appends a block 
 * to end of block list in a given heap */
void HeapAppendBlockToList(Heap_t *Heap, HeapBlock_t *Block)
{
	/* Variables needed for iteration */
	HeapBlock_t *CurrBlock = NULL;

	/* Sanitize parameter */
	assert(Block != NULL);

	/* Select the list */
	if (Block->Flags & BLOCK_VERY_LARGE) {
		if (Heap->CustomBlocks == NULL) {
			Heap->CustomBlocks = Block;
			return;
		}
		CurrBlock = Heap->CustomBlocks;
	}
	else if (Block->Flags & BLOCK_ALIGNED) {
		CurrBlock = Heap->PageBlocks;
	}
	else {
		CurrBlock = Heap->Blocks;
	}

	/* Loop to end, we want to append */
	while (CurrBlock->Link)
		CurrBlock = CurrBlock->Link;

	/* Set as end */
	CurrBlock->Link = Block;
	Block->Link = NULL;
}

/* Helper function that enumerates a given block
 * list, since we need to support multiple lists, reuse this */
void HeapStatsCounter(HeapBlock_t *Block, size_t *BlockCounter, size_t *NodeCounter,
	size_t *NodesAllocated, size_t *BytesAllocated)
{
	while (Block)
	{
		/* Node iterator */
		HeapNode_t *CurrentNode = Block->Nodes;

		/* Iterate */
		while (CurrentNode)
		{
			/* Stats */
			(*NodesAllocated) += (CurrentNode->Flags & NODE_ALLOCATED) ? 1 : 0;
			(*BytesAllocated) += (CurrentNode->Flags & NODE_ALLOCATED) ? CurrentNode->Length : 0;

			/* Increase the node count */
			(*NodeCounter) += 1;

			/* Next Node */
			CurrentNode = CurrentNode->Link;
		}

		/* What kind of block? */
		(*BlockCounter) += 1;

		/* Next Block */
		Block = Block->Link;
	}
}

/* Helper function that enumerates the given heap 
 * and prints out different allocation stats of heap */
void HeapPrintStats(Heap_t *Heap)
{
	/* Variables needed for enumeration of 
	 * the heap and stat variables */
	Heap_t *pHeap = Heap;
	size_t StatNodeCount = 0;
	size_t StatBlockCount = 0;
	size_t StatBytesAllocated = 0;
	size_t StatNodesAllocated = 0;

	size_t StatBlockPageCount = 0;
	size_t StatBlockBigCount = 0;
	size_t StatBlockNormCount = 0;

	size_t StatNodePageCount = 0;
	size_t StatNodeBigCount = 0;
	size_t StatNodeNormCount = 0;
   
	size_t StatNodePageAllocated = 0;
	size_t StatNodeBigAllocated = 0;
	size_t StatNodeNormAllocated = 0;

	size_t StatNodePageAllocatedBytes = 0;
	size_t StatNodeBigAllocatedBytes = 0;
	size_t StatNodeNormAllocatedBytes = 0;

	/* Sanitize heap param, if NULL
	 * we want to use the kernel heap instead */
	if (pHeap == NULL)
		pHeap = &GlbKernelHeap;

	/* Count block-list */
	HeapStatsCounter(pHeap->Blocks, &StatBlockNormCount, &StatNodeNormCount,
		&StatNodeNormAllocated, &StatNodeNormAllocatedBytes);
	HeapStatsCounter(pHeap->PageBlocks, &StatBlockPageCount, &StatNodePageCount,
		&StatNodePageAllocated, &StatNodePageAllocatedBytes);
	HeapStatsCounter(pHeap->CustomBlocks, &StatBlockBigCount, &StatNodeBigCount,
		&StatNodeBigAllocated, &StatNodeBigAllocatedBytes);

	/* Add stuff together */
	StatBlockCount = (StatBlockNormCount + StatBlockPageCount + StatBlockBigCount);
	StatNodeCount = (StatNodeNormCount + StatNodePageCount + StatNodeBigCount);
	StatNodesAllocated = (StatNodeNormAllocated + StatNodePageAllocated + StatNodeBigAllocated);
	StatBytesAllocated = (StatNodeNormAllocatedBytes + StatNodePageAllocatedBytes + StatNodeBigAllocatedBytes);

	/* Done, print */
	LogDebug("HEAP", "Heap Stats (Salloc ptr at 0x%x, next data at 0x%x", 
		pHeap->MemHeaderCurrent, pHeap->MemStartData);
	LogDebug("HEAP", "  -- Blocks Total: %u", StatBlockCount);
	LogDebug("HEAP", "     -- Normal Blocks: %u", StatBlockNormCount);
	LogDebug("HEAP", "     -- Page Blocks: %u", StatBlockPageCount);
	LogDebug("HEAP", "     -- Big Blocks: %u", StatBlockBigCount);
	LogDebug("HEAP", "  -- Nodes Total: %u (Allocated %u)", StatNodeCount, StatNodesAllocated);
	LogDebug("HEAP", "     -- Normal Nodes: %u (Bytes - %u)", StatNodeNormCount, StatNodeNormAllocatedBytes);
	LogDebug("HEAP", "     -- Page Nodes: %u (Bytes - %u)", StatNodePageCount, StatNodePageAllocatedBytes);
	LogDebug("HEAP", "     -- Big Nodes: %u (Bytes - %u)", StatNodeBigCount, StatNodeBigAllocatedBytes);
	LogDebug("HEAP", "  -- Bytes Total Allocated: %u", StatBytesAllocated);
}

/* Helper to allocate and create a block for a given heap
 * it will allocate from the recycler if possible */
HeapBlock_t *HeapCreateBlock(Heap_t *Heap, 
	size_t Size, Addr_t Mask, Flags_t Flags, const char *Identifier)
{
	/* Allocate a block & node */
	HeapBlock_t *hBlock = NULL;
	HeapNode_t *hNode = NULL;

	/* Assert that the requested expansion does
	 * not cause us to run out of memory */
	assert((Heap->MemStartData + Size) < Heap->HeapEnd);

	/* Sanity Recycler */
	if (Heap->BlockRecycler != NULL) {
		hBlock = Heap->BlockRecycler;
		Heap->BlockRecycler = Heap->BlockRecycler->Link;
	}
	else
		hBlock = (HeapBlock_t*)HeapSAllocator(Heap, sizeof(HeapBlock_t));

	/* Sanitize that the allocated block 
	 * is not NULL */
	assert(hBlock != NULL);

	/* Sanity Recycler */
	if (Heap->NodeRecycler != NULL)
	{
		/* Pop */
		hNode = Heap->NodeRecycler;
		Heap->NodeRecycler = Heap->NodeRecycler->Link;
	}
	else
		hNode = (HeapNode_t*)HeapSAllocator(Heap, sizeof(HeapNode_t));

	/* Sanitize that the allocated block
	 * is not NULL */
	assert(hNode != NULL);

	/* Set Members */
	hBlock->AddressStart = Heap->MemStartData;
	hBlock->AddressEnd = (Heap->MemStartData + Size - 1);
	hBlock->BytesFree = Size;
	hBlock->Flags = Flags;
	hBlock->Link = NULL;
	hBlock->Nodes = hNode;
	hBlock->Mask = Mask;

	/* Setup node with identifier */
	memcpy(&hNode->Identifier[0], Identifier == NULL ? GlbKernelUnknown : Identifier,
		Identifier == NULL ? strlen(GlbKernelUnknown) : strnlen(Identifier, 7));
	hNode->Identifier[7] = '\0';
	hNode->Address = Heap->MemStartData;
	hNode->Link = NULL;
	hNode->Flags = 0;
	hNode->Length = Size;

	/* Increament Address */
	Heap->MemStartData += Size;

	/* Done! */
	return hBlock;
}

/* Helper to expand the heap with a given size, now 
 * it also heavily depends on which kind of allocation is being made */
void HeapExpand(Heap_t *Heap, size_t Size, Addr_t Mask, Flags_t Flags, const char *Identifier)
{
	/* Variable */
	HeapBlock_t *hBlock;

	/* Which kind of expansion are we making? */
	if (Flags & ALLOCATION_BIG) {
		hBlock = HeapCreateBlock(Heap, Size, Mask, BLOCK_VERY_LARGE, Identifier);
	}
	else if (Flags & ALLOCATION_PAGEALIGN) {
		hBlock = HeapCreateBlock(Heap, HEAP_LARGE_BLOCK, Mask, BLOCK_ALIGNED, Identifier);
	}
	else {
		hBlock = HeapCreateBlock(Heap, HEAP_NORMAL_BLOCK, Mask, BLOCK_NORMAL, Identifier);
	}

	/* Add it to list */
	HeapAppendBlockToList(Heap, hBlock);
}

/**************************************/
/********** Heap Allocation ***********/
/**************************************/

/* Helper function for the primary allocation function, this 
 * 'sub'-allocates <size> in a given <block> from the heap */
Addr_t HeapAllocateSizeInBlock(Heap_t *Heap, HeapBlock_t *Block, 
	size_t Size, size_t Alignment, const char *Identifier)
{
	/* Variables needed for node iteration */
	HeapNode_t *CurrNode = Block->Nodes, 
			   *PrevNode = NULL;
	Addr_t RetAddr = 0;

	/* Standard block allocation algorithm */
	while (CurrNode)
	{
		/* Do some initial sanity checks
		 * for whether this node is valid */
		if (CurrNode->Flags & NODE_ALLOCATED
			|| CurrNode->Length < Size) {
			goto Skip;
		}

		/* Allocate, two cases, either exact
		 * match in size or we make a new header */
		if (CurrNode->Length == Size
			|| Block->Flags & BLOCK_VERY_LARGE)
		{
			/* Well that matched awfully well
			 * -> Custom allocation, one note, set information */
			memcpy(&CurrNode->Identifier[0], Identifier == NULL ? GlbKernelUnknown : Identifier,
				Identifier == NULL ? strlen(GlbKernelUnknown) : strnlen(Identifier, 7));
			CurrNode->Identifier[7] = '\0';
			CurrNode->Flags = NODE_ALLOCATED;

			/* Store address, update information */
			RetAddr = CurrNode->Address;
			Block->BytesFree -= Size;

			/* We are done! */
			break;
		}
		else
		{
			/* Make new node
			 * Insert it before this */
			HeapNode_t *hNode = NULL;

			/* Before continuining we want to make
			 * sure this node actually have room in
			 * case we need alignment */
			if (Alignment != 0
				&& !ISALIGNED(CurrNode->Address, Alignment)) {
				if (CurrNode->Length < ALIGN(Size, Alignment, 1)) {
					goto Skip;
				}
			}

			/* Sanitize the recycler before
			 * we actually allocate a new node */
			if (Heap->NodeRecycler != NULL)
			{
				/* Pop */
				hNode = Heap->NodeRecycler;
				Heap->NodeRecycler = Heap->NodeRecycler->Link;
			}
			else
				hNode = (HeapNode_t*)HeapSAllocator(Heap, sizeof(HeapNode_t));

			/* Sanitize the node! */
			assert(hNode != NULL);

			/* Fill in details in the new node
			 * and set the identifier */
			memcpy(&CurrNode->Identifier[0], Identifier == NULL ? GlbKernelUnknown : Identifier,
				Identifier == NULL ? strlen(GlbKernelUnknown) : strnlen(Identifier, 7));
			CurrNode->Identifier[7] = '\0';

			/* Set links and set it allocated */
			hNode->Address = CurrNode->Address;
			hNode->Flags = NODE_ALLOCATED;
			hNode->Link = CurrNode;

			/* Handle alignment if neccessary */
			if (Alignment != 0
				&& !ISALIGNED(hNode->Address, Alignment)) {

				/* Adjust size by the difference of address
				 * and then store it */
				Size += Alignment - (hNode->Address % Alignment);
				hNode->Length = Size;

				/* Adjust address aswell */
				RetAddr = ALIGN(hNode->Address, Alignment, 1);
			}
			else {
				/* Simply store this */
				hNode->Length = Size;

				/* Store the return address */
				RetAddr = hNode->Address;
			}

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

	Skip:
		/* Next node, search for a free */
		PrevNode = CurrNode;
		CurrNode = CurrNode->Link;
	}

	/* Return Address */
	return RetAddr;
}

/* Helper for mapping in pages as soon as they
 * are allocated, used by allocation flags */
void HeapCommitPages(Addr_t Address, size_t Size, Addr_t Mask)
{
	/* Variables  */
	size_t Pages = DIVUP(Size, PAGE_SIZE);
	size_t i = 0;

	/* Sanitize the page boundary
	 * Do we step across page boundary? */
	if ((Address & PAGE_MASK)
		!= ((Address + Size - 1) & PAGE_MASK))
		Pages++;

	/* Map in the pages */
	for (; i < Pages; i++)
	{
		if (!AddressSpaceGetMap(AddressSpaceGetCurrent(), Address + (i * PAGE_SIZE)))
			AddressSpaceMap(AddressSpaceGetCurrent(), 
				Address + (i * PAGE_SIZE), PAGE_SIZE, Mask, 0);
	}
}

/* Finds a suitable block for allocation
 * and allocates in that block, this is primary 
 * allocator of the heap */
Addr_t HeapAllocate(Heap_t *Heap, size_t Size, 
	Flags_t Flags, size_t Alignment, Addr_t Mask, const char *Identifier)
{
	/* Find a block that match our 
	 * requirements */
	HeapBlock_t *CurrentBlock = NULL;
	size_t AdjustedAlign = Alignment;
	size_t AdjustedSize = Size;
	Addr_t RetVal = 0;

	/* Add some block-type flags 
	 * based upon size of requested allocation 
	 * we want to use our normal blocks for smaller allocations 
	 * and the page-aligned for larger ones */
	if (ALLOCISNORMAL(Flags) && Size >= HEAP_NORMAL_BLOCK) {
		AdjustedSize = ALIGN(Size, PAGE_SIZE, 1);
		Flags |= ALLOCATION_PAGEALIGN;
	}
	if (ALLOCISNOTBIG(Flags) && AdjustedSize >= HEAP_LARGE_BLOCK) {
		Flags |= ALLOCATION_BIG;
	}
	if (ALLOCISPAGE(Flags) && !ISALIGNED(AdjustedSize, PAGE_SIZE)) {
		AdjustedSize = ALIGN(AdjustedSize, PAGE_SIZE, 1);
	}

	/* Acquire the lock */
	CriticalSectionEnter(&Heap->Lock);

	/* Access the proper list based on flags */
	if (Flags & ALLOCATION_BIG) {
		CurrentBlock = Heap->CustomBlocks;
	}
	else if (Flags & ALLOCATION_PAGEALIGN) {
		CurrentBlock = Heap->PageBlocks;
	}
	else {
		CurrentBlock = Heap->Blocks;
	}

	/* Now lets iterate the heap */
	while (CurrentBlock)
	{
		/* Check which type of allocation is 
		 * being made, we have three types: 
		 * Standard-aligned allocations
		 * Page-aligned allocations 
		 * Custom allocations (large) */

		/* Sanitize both that enough space is free 
		 * but ALSO that the block has higher mask */
		if ((CurrentBlock->BytesFree < AdjustedSize)
			|| (CurrentBlock->Mask < Mask)) {
			goto Skip;
		}

		/* Try to make the allocation, THIS CAN FAIL */
		RetVal = HeapAllocateSizeInBlock(Heap, CurrentBlock,
			AdjustedSize, AdjustedAlign, Identifier);

		/* Check if succeded 
		 * then we have an allocation */
		if (RetVal != 0)
			break;

	Skip:
		/* Next Block */
		CurrentBlock = CurrentBlock->Link;
	}

	/* Sanitize
	 * If return value is not 0 it means
	 * our allocation was made! */
	if (RetVal != 0) {

		/* Were we asked to commit pages? */
		if (Flags & ALLOCATION_COMMIT) {
			HeapCommitPages(RetVal, AdjustedSize, Mask);
		}

		/* Release lock */
		CriticalSectionLeave(&Heap->Lock);

		/* Done! */
		return RetVal;
	}

	/* If we reach here, expand and research */
	HeapExpand(Heap, AdjustedSize, Mask, Flags, Identifier);

	/* Release lock */
	CriticalSectionLeave(&Heap->Lock);

	/* Recursive Call */
	return HeapAllocate(Heap, AdjustedSize, Flags, AdjustedAlign, Mask, Identifier);
}

/**************************************/
/*********** Heap Freeing *************/
/**************************************/

/* Helper for the primary freeing routine, it free's 
 * an address in the correct block, it's a bit more complicated
 * as it supports merging with sibling nodes  */
void HeapFreeAddressInNode(Heap_t *Heap, HeapBlock_t *Block, Addr_t Address)
{
	/* Variables for iteration */
	HeapNode_t *CurrNode = Block->Nodes, *PrevNode = NULL;

	/* Standard block freeing algorithm */
	while (CurrNode != NULL)
	{
		/* Calculate end and start */
		Addr_t aStart = CurrNode->Address;
		Addr_t aEnd = CurrNode->Address + CurrNode->Length - 1;

		/* Check if address is a part of this node 
		 * And we do need to check like this as address
		 * might have been aligned */
		if (Address >= aStart && Address < aEnd)
		{
			/* We found the relevant node, reset it's data 
			 * Flags, identification, but ofc not address */
			CurrNode->Flags = 0;
			
			/* Update information */
			Block->BytesFree += CurrNode->Length;

			/* CHECK IF WE CAN MERGE!! */
			if (PrevNode == NULL
				&& CurrNode->Link == NULL)
				return;

			/* Can we merge with previous? */
			if (PrevNode != NULL
				&& !(PrevNode->Flags & NODE_ALLOCATED))
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
				if (Heap->NodeRecycler == NULL)
					Heap->NodeRecycler = CurrNode;
				else
				{
					/* Front-us */
					CurrNode->Link = Heap->NodeRecycler;
					Heap->NodeRecycler = CurrNode;
				}
			}
			else if (CurrNode->Link != NULL
				&& !(CurrNode->Flags & NODE_ALLOCATED))
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
				if (Heap->NodeRecycler == NULL)
					Heap->NodeRecycler = CurrNode;
				else
				{
					/* Front-us */
					CurrNode->Link = Heap->NodeRecycler;
					Heap->NodeRecycler = CurrNode;
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

/* Helper for locating the correct block, as we have
 * three different lists with blocks in, not very quick
 * untill we convert it to a binary tree */
HeapBlock_t *HeapFreeLocator(HeapBlock_t *List, Addr_t Address)
{
	/* Find a block that match the 
	 * given address */
	HeapBlock_t *Block = List;

	/* Iterate all blocks in list */
	while (Block) {
		if (Block->AddressStart <= Address
				&& Block->AddressEnd > Address) {
			return Block;
		}
		Block = Block->Link;
	}

	/* Otherwise, no */
	return NULL;
}

/* Finds the appropriate block
 * that should contain our node */
void HeapFree(Heap_t *Heap, Addr_t Address)
{
	/* Find a block that match our address */
	HeapBlock_t *Block = NULL;

	/* Acquire the lock */
	CriticalSectionEnter(&Heap->Lock);

	/* Try to locate the block (normal) */
	Block = HeapFreeLocator(Heap->Blocks, Address);
	if (Block == NULL) {
		Block = HeapFreeLocator(Heap->PageBlocks, Address);
	}
	if (Block == NULL) {
		Block = HeapFreeLocator(Heap->CustomBlocks, Address);
	}

	/* We don't want invalid frees */
	assert(Block != NULL);

	/* Do the free */
	HeapFreeAddressInNode(Heap, Block, Address);
	
	/* Release the lock */
	CriticalSectionLeave(&Heap->Lock);
}

/**************************************/
/*********** Heap Querying ************/
/**************************************/

/* This function is basicilly just a node-lookup function
 * that's used to find information about the node */
HeapNode_t *HeapQueryAddressInNode(HeapBlock_t *Block, Addr_t Address)
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
			/* Yay! */
			return CurrNode;
		}

		/* Next node, search for the allocated block */
		PrevNode = CurrNode;
		CurrNode = CurrNode->Link;
	}

	/* Not found */
	return NULL;
}

/* Finds the appropriate block
 * that should contain our node */
HeapNode_t *HeapQuery(Heap_t *Heap, Addr_t Addr)
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
			/* Done! */
			return HeapQueryAddressInNode(CurrBlock, Addr);
		}

		/* Next Block */
		CurrBlock = CurrBlock->Link;
	}

	/* Dayum */
	return NULL;
}

/* Queries memory information about a heap
 * useful for processes and such */
int HeapQueryMemoryInformation(Heap_t *Heap, size_t *BytesInUse, size_t *BlocksAllocated)
{
	/* Variables */
	HeapBlock_t *CurrentBlock, *PreviousBlock;
	size_t NodeCount = 0;
	size_t BytesAllocated = 0;
	size_t NodesAllocated = 0;

	/* Sanity 
	 * Make sure we have a heap */
	if (Heap == NULL)
		return -1;

	/* Count Nodes */
	CurrentBlock = Heap->Blocks, PreviousBlock = NULL;
	while (CurrentBlock)
	{
		/* We need to iterate nodes */
		HeapNode_t *CurrentNode = CurrentBlock->Nodes, *PreviousNode = NULL;
		while (CurrentNode)
		{
			/* Stats */
			NodesAllocated = (CurrentNode->Flags & NODE_ALLOCATED) ? NodesAllocated + 1 : NodesAllocated;
			BytesAllocated = (CurrentNode->Flags & NODE_ALLOCATED) ? (BytesAllocated + CurrentNode->Length) : BytesAllocated;
			NodeCount++;

			/* Next Node */
			PreviousNode = CurrentNode;
			CurrentNode = CurrentNode->Link;
		}

		/* Next Block */
		PreviousBlock = CurrentBlock;
		CurrentBlock = CurrentBlock->Link;
	}

	/* Yay, set stuff */
	if (BytesInUse != NULL)
		*BytesInUse = BytesAllocated;
	if (BlocksAllocated != NULL)
		*BlocksAllocated = NodesAllocated;

	/* No probs */
	return 0;
}

/**************************************/
/******** Heap Initialization *********/
/**************************************/

/* This initializes the kernel heap and 
 * readies the first few blocks for allocation
 * this MUST be called before any calls to *mallocs */
void HeapInit(void)
{
	/* Reset */
	GlbKernelHeap.IsUser = 0;
	GlbKernelHeap.HeapBase = MEMORY_LOCATION_HEAP;
	GlbKernelHeap.MemStartData = GlbKernelHeap.HeapBase + MEMORY_STATIC_OFFSET;
	GlbKernelHeap.MemHeaderCurrent = GlbKernelHeap.HeapBase;
	GlbKernelHeap.MemHeaderMax = GlbKernelHeap.HeapBase;
	GlbKernelHeap.HeapEnd = MEMORY_LOCATION_HEAP_END;

	/* Set null */
	GlbKernelHeap.BlockRecycler = NULL;
	GlbKernelHeap.NodeRecycler = NULL;

	/* Initiate the global spinlock */
	CriticalSectionConstruct(&GlbKernelHeap.Lock, CRITICALSECTION_REENTRANCY);

	/* Create initial nodes
	 * so we save the initial creation */
	GlbKernelHeap.Blocks = HeapCreateBlock(&GlbKernelHeap,
		HEAP_NORMAL_BLOCK, MEMORY_MASK_DEFAULT, BLOCK_NORMAL, GlbKernelUnknown);
	GlbKernelHeap.PageBlocks = HeapCreateBlock(&GlbKernelHeap,
		HEAP_LARGE_BLOCK, MEMORY_MASK_DEFAULT, BLOCK_ALIGNED, GlbKernelUnknown);
	GlbKernelHeap.CustomBlocks = NULL;

	/* Reset Stats */
	GlbKernelHeap.BytesAllocated = 0;
	GlbKernelHeap.NumAllocs = 0;
	GlbKernelHeap.NumFrees = 0;
	GlbKernelHeap.NumPages = 0;

	/* Heap is now ready to use! */
}

/* This function allocates a 'third party' heap that
 * can be used like a memory region for allocations, usefull
 * for servers, shared memory, processes etc */
Heap_t *HeapCreate(Addr_t HeapAddress, Addr_t HeapEnd, int UserHeap)
{
	/* Allocate a heap on the heap
	 * Heapception */
	Heap_t *Heap = (Heap_t*)kmalloc(sizeof(Heap_t));

	/* Reset */
	Heap->IsUser = UserHeap;
	Heap->HeapBase = HeapAddress;
	Heap->MemStartData = HeapAddress + MEMORY_STATIC_OFFSET;
	Heap->MemHeaderCurrent = HeapAddress;
	Heap->MemHeaderMax = HeapAddress;
	Heap->HeapEnd = HeapEnd;

	/* Set null */
	Heap->BlockRecycler = NULL;
	Heap->NodeRecycler = NULL;

	/* Initiate the global spinlock */
	CriticalSectionConstruct(&Heap->Lock, CRITICALSECTION_REENTRANCY);

	/* Create a normal node + large node to 
	 * save the initial creations */
	Heap->Blocks = HeapCreateBlock(Heap, HEAP_NORMAL_BLOCK, 
		MEMORY_MASK_DEFAULT,  BLOCK_NORMAL, GlbKernelUnknown);
	Heap->PageBlocks = HeapCreateBlock(Heap, HEAP_LARGE_BLOCK,
		MEMORY_MASK_DEFAULT, BLOCK_ALIGNED, GlbKernelUnknown);
	Heap->CustomBlocks = NULL;

	/* Reset Stats */
	Heap->BytesAllocated = 0;
	Heap->NumAllocs = 0;
	Heap->NumFrees = 0;
	Heap->NumPages = 0;

	/* Done */
	return Heap;
}

/**************************************/
/*********** Heap Utilities ***********/
/**************************************/

/* Used for validation that an address is allocated
 * within the given heap, this can be used for security
 * or validation purposes, use NULL for kernel heap */
int HeapValidateAddress(Heap_t *Heap, Addr_t Address)
{
	/* Vars */
	Heap_t *pHeap = Heap;

	/* Sanity */
	if (pHeap == NULL)
		pHeap = &GlbKernelHeap;

	/* Find Addr */
	HeapNode_t *MemInfo = HeapQuery(pHeap, Address);

	/* Sanity */
	if (MemInfo == NULL
		|| !(MemInfo->Flags & NODE_ALLOCATED))
		return -1;

	/* Yay */
	return 0;
}

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation and aligned to PAGE_ALIGN with memory <Mask> */
void *kmalloc_apm(size_t Size, Addr_t *Ptr, Addr_t Mask)
{
	/* Variables for kernel allocation
	 * Setup some default stuff */
	Addr_t RetAddr = 0;

	/* Sanitize size in kernel allocations 
	 * we need to extra sensitive */
	assert(Size != 0);

	/* Do the call */
	RetAddr = HeapAllocate(&GlbKernelHeap, Size, 
		ALLOCATION_COMMIT | ALLOCATION_PAGEALIGN, 
		0, Mask, GlbKernelUnknown);

	/* Sanitize null-allocs in kernel allocations
	 * we need to extra sensitive, we also assert
	 * the alignment */
	assert(RetAddr != 0 && (RetAddr & ATTRIBUTE_MASK) == 0);

	/* Now, get physical mapping */
	*Ptr = AddressSpaceGetMap(AddressSpaceGetCurrent(), RetAddr);

	/* Done */
	return (void*)RetAddr;
}

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation and aligned to PAGE_ALIGN */
void *kmalloc_ap(size_t Size, Addr_t *Ptr)
{
	/* Variables for kernel allocation
	 * Setup some default stuff */
	Addr_t RetAddr = 0;

	/* Sanitize size in kernel allocations 
	 * we need to extra sensitive */
	assert(Size != 0);

	/* Do the call */
	RetAddr = HeapAllocate(&GlbKernelHeap, Size, 
		ALLOCATION_COMMIT | ALLOCATION_PAGEALIGN, 
		0, MEMORY_MASK_DEFAULT, GlbKernelUnknown);

	/* Sanitize null-allocs in kernel allocations
	 * we need to extra sensitive, we also assert
	 * the alignment */
	assert(RetAddr != 0 && (RetAddr & ATTRIBUTE_MASK) == 0);

	/* Now, get physical mapping */
	*Ptr = AddressSpaceGetMap(AddressSpaceGetCurrent(), RetAddr);

	/* Done */
	return (void*)RetAddr;
}

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation */
void *kmalloc_p(size_t Size, Addr_t *Ptr)
{
	/* Variables for kernel allocation
	 * Setup some default stuff */
	Addr_t RetAddr = 0;

	/* Sanitize size in kernel allocations 
	 * we need to extra sensitive */
	assert(Size > 0);

	/* Do the call */
	RetAddr = HeapAllocate(&GlbKernelHeap, Size, 
		ALLOCATION_COMMIT, HEAP_STANDARD_ALIGN, 
		MEMORY_MASK_DEFAULT, NULL);

	/* Sanitize size in kernel allocations 
	 * we need to extra sensitive */
	assert(RetAddr != 0);

	/* Now, get physical mapping */
	*Ptr = AddressSpaceGetMap(AddressSpaceGetCurrent(), RetAddr);

	/* Done */
	return (void*)RetAddr;
}

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory 
 * the memory returned is PAGE_ALIGNED */
void *kmalloc_a(size_t Size)
{
	/* Variables for kernel allocation
	 * Setup some default stuff */
	Addr_t RetAddr = 0;

	/* Sanitize size in kernel allocations 
	 * we need to extra sensitive */
	assert(Size != 0);

	/* Do the call */
	RetAddr = HeapAllocate(&GlbKernelHeap, Size, 
		ALLOCATION_COMMIT | ALLOCATION_PAGEALIGN, 
		0, MEMORY_MASK_DEFAULT, GlbKernelUnknown);

	/* Sanitize null-allocs in kernel allocations
	 * we need to extra sensitive, we also assert
	 * the alignment */
	assert(RetAddr != 0 && (RetAddr & ATTRIBUTE_MASK) == 0);

	/* Done */
	return (void*)RetAddr;
}

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory */
void *kmalloc(size_t Size)
{
	/* Variables for kernel allocation
	 * Setup some default stuff */
	Addr_t RetAddr = 0;

	/* Sanitize size in kernel allocations 
	 * we need to extra sensitive */
	assert(Size > 0);

	/* Do the call */
	RetAddr = HeapAllocate(&GlbKernelHeap, Size, 
		ALLOCATION_COMMIT, HEAP_STANDARD_ALIGN, 
		MEMORY_MASK_DEFAULT, NULL);

	/* Sanitize size in kernel allocations 
	 * we need to extra sensitive */
	assert(RetAddr != 0);
	
	/* Done */
	return (void*)RetAddr;
}

/* kfree 
 * Wrapper for the HeapFree that essentially 
 * just calls it with the kernel heap as argument */
void kfree(void *p)
{
	/* Sanity */
	assert(p != NULL);

	/* Free */
	HeapFree(&GlbKernelHeap, (Addr_t)p);
}

/**************************************/
/******** Heap Testing Suite  *********/
/**************************************/
void heap_test(void)
{
	/* Do some debugging */
	uint32_t phys1 = 0, phys2 = 0, i = 0;
	void *res1, *res2, *res3, *res4, *res5, *res6;

	HeapPrintStats(&GlbKernelHeap);

	printf(" >> Performing small allocs & frees (a few)\n");
	res1 = kmalloc(0x30);
	res2 = kmalloc(0x50);
	res3 = kmalloc(0x130);
	res4 = kmalloc(0x180);
	res5 = kmalloc(0x600);
	res6 = kmalloc(0x3000);

	HeapPrintStats(&GlbKernelHeap);

	printf(" Alloc1 (0x30): 0x%x, Alloc2 (0x50): 0x%x, Alloc3 (0x130): 0x%x, Alloc4 (0x180): 0x%x\n",
		(uint32_t)res1, (uint32_t)res2, (uint32_t)res3, (uint32_t)res4);
	printf(" Alloc5 (0x600): 0x%x, Alloc6 (0x3000): 0x%x\n", (uint32_t)res5, (uint32_t)res6);

	printf(" Freeing Alloc5, 2 & 3...\n");
	kfree(res5);
	kfree(res2);
	kfree(res3);

	HeapPrintStats(&GlbKernelHeap);

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

	HeapPrintStats(&GlbKernelHeap);

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

	HeapPrintStats(&GlbKernelHeap);

	printf(" >> Performing allocations (150)\n");
	
	for (i = 0; i < 50; i++)
	{
		kmalloc(0x50);
		kmalloc(0x180);
		kmalloc(0x3000);
	}

	HeapPrintStats(&GlbKernelHeap);
	
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
