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
* MollenOS Heap Manager
* Basic Heap Manager
* No contraction for now, for simplicity.
*/

#ifndef _MCORE_HEAP_H_
#define _MCORE_HEAP_H_

/* Includes */
#include <Arch.h>
#include <CriticalSection.h>

#include <stdint.h>
#include <crtdefs.h>

/***************************
Heap Management
***************************/
#define MEMORY_STATIC_OFFSET	0x400000 /* Reserved Header Space */
#define HEAP_NORMAL_BLOCK		0x1000
#define HEAP_LARGE_BLOCK		0x10000

/* 12 bytes overhead */
typedef struct _HeapNode
{
	/* Address */
	Addr_t Address;

	/* Status */
	int Allocated;

	/* Length */
	size_t Length;

	/* Link */
	struct _HeapNode *Link;

} HeapNode_t;

#define BLOCK_NORMAL			0x0
#define BLOCK_LARGE				0x1
#define BLOCK_VERY_LARGE		0x2
#define AddrIsAligned(x)		((x & 0xFFF) == 0)

#define ALLOCATION_NORMAL		0x0
#define ALLOCATION_ALIGNED		0x1
#define ALLOCATION_SPECIAL		0x2

/* A block descriptor, contains a 
 * memory range */
typedef struct _HeapBlock
{
	/* Start - End Address */
	Addr_t AddressStart;
	Addr_t AddressEnd;

	/* Node Flags */
	int Flags;

	/* Stats */
	size_t BytesFree;

	/* Next in Linked List */
	struct _HeapBlock *Link;

	/* Head of Header List */
	struct _HeapNode *Nodes;

} HeapBlock_t;

/* A heap */
typedef struct _HeapArea
{
	/* Info */
	Addr_t HeapBase;
	Addr_t MemStartData;
	Addr_t MemHeaderCurrent;
	Addr_t MemHeaderMax;

	/* UserHeap? */
	int IsUser;

	/* Stats */
	size_t BytesAllocated;
	size_t NumAllocs;
	size_t NumFrees;
	size_t NumPages;

	/* Lock */
	CriticalSection_t Lock;

	/* Recyclers */
	HeapBlock_t *BlockRecycler;
	HeapNode_t *NodeRecycler;

	/* Head of Node List */
	struct _HeapBlock *Blocks;

} Heap_t;

/* Initializer & Maintience */
_CRT_EXTERN void HeapInit(void);
_CRT_EXTERN Heap_t *HeapCreate(Addr_t HeapAddress, int UserHeap);
_CRT_EXTERN uint32_t HeapGetCount(void);
_CRT_EXTERN void HeapPrintStats(Heap_t *Heap);
_CRT_EXTERN void HeapReap(void);
_CRT_EXTERN int HeapValidateAddress(Heap_t *Heap, Addr_t Address);

/* Kernel Allocations */
_CRT_EXPORT void *kmalloc_ap(size_t sz, Addr_t *p);
_CRT_EXPORT void *kmalloc_p(size_t sz, Addr_t *p);
_CRT_EXPORT void *kmalloc_a(size_t sz);
_CRT_EXPORT void *kmalloc(size_t sz);
_CRT_EXPORT void *kcalloc(size_t nmemb, size_t size);
_CRT_EXPORT void *krealloc(void *ptr, size_t size);

/* Kernel Freeing */
_CRT_EXPORT void kfree(void *p);

/* Custom Allocation */
_CRT_EXTERN void *umalloc(Heap_t *Heap, size_t Size);
_CRT_EXTERN void ufree(Heap_t *Heap, void *Ptr);

#endif