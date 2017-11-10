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

#ifndef _MCORE_HEAP_H_
#define _MCORE_HEAP_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Includes 
 * - System */
#include <criticalsection.h>

/***************************
 * Heap Management
 ***************************/

/* These are defined from trial and error 
 * and should probably never be any LESS than
 * these values, of course they could be higher */
#define MEMORY_STATIC_OFFSET	0x400000 
#define HEAP_NORMAL_BLOCK		0x2000
#define HEAP_LARGE_BLOCK		0x20000

#define HEAP_IDENT_SIZE			8
#define HEAP_STANDARD_ALIGN		4

/* The basic HeapNode, this describes a node in a block
 * of memory, and contains basic properties. 
 * The size of this is 12 bytes + HEAP_IDENT_SIZE */
typedef struct _HeapNode
{
	/* Node Identifier 
	 * A very short-text (optional)
	 * describing the allocation */
	char Identifier[HEAP_IDENT_SIZE];

	/* Base Address of the allocation
	 * made, used for finding the allocation */
	uintptr_t Address;

	/* Node Flags 
	 * Used to set different status flags for
	 * this node */
	Flags_t Flags;

	/* Node Length 
	 * Describes the length of this allocation or
	 * just the length of this unallocated block */
	size_t Length;

	/* Node Link */
	struct _HeapNode *Link;

} HeapNode_t;

/* Heap node flags, used by <Flags> in 
 * the heap node structure */
#define NODE_ALLOCATED				0x1

/* A block descriptor in the heap, 
 * contains a memory range, used like a bucket */
typedef struct _HeapBlock
{
	/* Block Address Range 
	 * Describes the Start - End Address */
	uintptr_t AddressStart;
	uintptr_t AddressEnd;

	/* Block Flags 
	 * Used to set different status flags for
	 * this block */
	Flags_t Flags;

	/* Block Address Mask
	 * This is for masked allocations 
	 * The mask must match or be less than this */
	uintptr_t Mask;

	/* Shortcut stats for quickly checking
	 * whether an allocation can be made */
	size_t BytesFree;

	/* The block link to the next
	 * block in the heap */
	struct _HeapBlock *Link;

	/* The children nodes of this
	 * memory block */
	struct _HeapNode *Nodes;

} HeapBlock_t;

/* Heap block flags, used by the <Flags> field
 * in a heap block to describe it */
#define BLOCK_NORMAL				0x0
#define BLOCK_ALIGNED				0x1
#define BLOCK_VERY_LARGE			0x2

/* This is the MCore Heap Structure 
 * and describes a memory region that can
 * be allocated from */
typedef struct _HeapRegion
{
	/* Heap Region 
	 * Stat variables, keep track of current allocations
	 * for headers and base memory */
	uintptr_t HeapBase;
	uintptr_t MemStartData;
	uintptr_t MemHeaderCurrent;
	uintptr_t MemHeaderMax;
	uintptr_t HeapEnd;

	/* Whether or not this is a user heap
	 * we need to know this when mapping in memory */
	int IsUser;

	/* Heap Region 
	 * Statistic variables, used to see interesting stuff */
	size_t BytesAllocated;
	size_t NumAllocs;
	size_t NumFrees;
	size_t NumPages;

	/* Heap Region Lock
	 * This is for critical stuff during allocation */
	CriticalSection_t Lock;

	/* Recyclers 
	 * Used by the region to reuse headers, nodes and blocks */
	HeapBlock_t *BlockRecycler;
	HeapNode_t *NodeRecycler;

	/* The children nodes of this
	 * memory region */
	struct _HeapBlock *Blocks;
	struct _HeapBlock *PageBlocks;
	struct _HeapBlock *CustomBlocks;

} Heap_t;

/* Allocation Flags */
#define ALLOCATION_COMMIT			0x1

/* HeapAllocate 
 * Finds a suitable block for allocation
 * and allocates in that block, this is primary
 * allocator of the heap */
__EXTERN uintptr_t HeapAllocate(Heap_t *Heap, size_t Size,
	Flags_t Flags, size_t Alignment, uintptr_t Mask, const char *Identifier);

/* HeapFree
 * Finds the appropriate block
 * that should contain our node */
__EXTERN void HeapFree(Heap_t *Heap, uintptr_t Addr);

/* HeapQueryMemoryInformation
 * Queries memory information about a heap
 * useful for processes and such */
__EXTERN int HeapQueryMemoryInformation(Heap_t *Heap,
	size_t *BytesInUse, size_t *BlocksAllocated);

/* HeapInit
 * This initializes the kernel heap and 
 * readies the first few blocks for allocation
 * this MUST be called before any calls to *mallocs */
__EXTERN void HeapInit(void);

/* HeapCreate
 * This function allocates a 'third party' heap that
 * can be used like a memory region for allocations, usefull
 * for servers, shared memory, processes etc */
__EXTERN Heap_t *HeapCreate(uintptr_t HeapAddress, uintptr_t HeapEnd, int UserHeap);

/* Helper function that enumerates the given heap 
 * and prints out different allocation stats of heap */
__EXTERN void HeapPrintStats(Heap_t *Heap);
__EXTERN void HeapReap(void);

/* Used for validation that an address is allocated
 * within the given heap, this can be used for security
 * or validation purposes, use NULL for kernel heap */
__EXTERN int HeapValidateAddress(Heap_t *Heap, uintptr_t Address);

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation and aligned to PAGE_ALIGN with memory <Mask> */
__EXTERN void *kmalloc_apm(size_t Size, uintptr_t *Ptr, uintptr_t Mask);

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation and aligned to PAGE_ALIGN */
__EXTERN void *kmalloc_ap(size_t Size, uintptr_t *Ptr);

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory
 * this function also returns the physical address 
 * of the allocation */
__EXTERN void *kmalloc_p(size_t Size, uintptr_t *Ptr);

/* Simply just a wrapper for HeapAllocate
 * with the kernel heap as argument 
 * but this does some basic validation and
 * makes sure pages are mapped in memory 
 * the memory returned is PAGE_ALIGNED */
__EXTERN void *kmalloc_a(size_t Size);

/* Simply just a wrapper for HeapAllocate
 * but this does some basic validation and
 * makes sure pages are mapped in memory */
__EXTERN void *kmalloc(size_t Size);

/* kfree 
 * Wrapper for the HeapFree that essentially 
 * just calls it with the kernel heap as argument */
__EXTERN void kfree(void *p);

#endif