/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Datastructure (Dynamic memory pool)
 * - Implementation of a memory pool as a binary-tree.
 */
//#define __TRACE

#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <utils/dynamic_memory_pool.h>
#include <string.h>

#define LEVEL_UP CurrentNode = CurrentNode->Parent; Depth--
#define GO_LEFT  CurrentNode = CurrentNode->Left; Depth++
#define GO_RIGHT CurrentNode = CurrentNode->Right; Depth++

static DynamicMemoryChunk_t*
CreateNode(
	_In_ DynamicMemoryChunk_t* Parent)
{
	DynamicMemoryChunk_t* Node = kmalloc(sizeof(DynamicMemoryChunk_t));
	if (!Node) {
		return NULL;
	}

	Node->Parent    = Parent;
	Node->Left      = NULL;
	Node->Right     = NULL;
	Node->Split     = 0;
	Node->Allocated = 0;
	return Node;
}

void 
DynamicMemoryPoolConstruct(
	_In_ DynamicMemoryPool_t* Pool,
	_In_ uintptr_t            StartAddress,
	_In_ size_t               Length,
	_In_ size_t               ChunkSize)
{
	assert(Pool != NULL);

	if (!IsPowerOfTwo(Length)) {
		ERROR("[utils] [dyn_mem_pool] length 0x%" PRIxIN " is not a power of two");
	}

    SpinlockConstruct(&Pool->SyncObject);
	Pool->StartAddress = StartAddress;
	Pool->Length       = Length;
	Pool->ChunkSize    = ChunkSize;
	Pool->Root         = CreateNode(NULL);
}

static void
DestroyNode(
	_In_ DynamicMemoryChunk_t* Node)
{
	if (!Node) {
		return;
	}

	DestroyNode(Node->Right);
	DestroyNode(Node->Left);
	kfree(Node);
}

void
DynamicMemoryPoolDestroy(
	_In_ DynamicMemoryPool_t* Pool)
{
	assert(Pool != NULL);

	DestroyNode(Pool->Root);
	Pool->Root = NULL;
}

static DynamicMemoryChunk_t*
FindNextParent(
	_In_  DynamicMemoryPool_t*  Pool,
	_In_  DynamicMemoryChunk_t* Node, 
	_Out_ int*                  Depth,
	_Out_ size_t*               AccumulatedLength)
{
	DynamicMemoryChunk_t* Finder       = Node;
	int                   FinderDepth  = *Depth;
	size_t                FinderLength = *AccumulatedLength;

	while (Finder) {
		if (Finder->Parent) {
			if (!Finder->Parent->Left->Allocated && Finder->Parent->Left != Finder) {
				FinderLength += (Pool->Length >> FinderDepth);
				*Depth = FinderDepth;
				*AccumulatedLength = FinderLength;
				return Finder->Parent->Left;
			}

			// adjust metrics
			if (Finder->Parent->Left == Finder) {
				FinderLength -= (Pool->Length >> FinderDepth);
			}
			FinderDepth--;
		}
		Finder = Finder->Parent;
	}
	return NULL;
}

static uintptr_t
IterativeAllocate(
	_In_ DynamicMemoryPool_t* Pool,
	_In_ size_t               Length)
{
	DynamicMemoryChunk_t* CurrentNode       = Pool->Root;
	size_t                AccumulatedLength = Pool->StartAddress;
	int                   Depth             = 0;
	int                   Finish            = 0;
	uintptr_t             Result            = 0;
	DynamicMemoryChunk_t* ParentNode;
	size_t                CurrentLength;
	size_t                NextLength;
	int                   GoDeeper;

	while (CurrentNode) {
		CurrentLength = Depth != 0 ? (Pool->Length >> Depth) : Pool->Length;
		NextLength    = (Pool->Length >> (Depth + 1));
		GoDeeper      = (Length <= NextLength && CurrentLength != Pool->ChunkSize);

		// Have we already made the allocation? And now we are performing bookkeeping?
		if (Finish) {
			if (Result) {
				// Book-keeping task to increase allocation performance (search performance)
				// If both children are allocated, mark us as allocated
				if (CurrentNode->Left->Allocated && CurrentNode->Right->Allocated) {
					CurrentNode->Allocated = 1;
				}
			}
			LEVEL_UP;
			continue;
		}

		// Go back up a level if this node is allocated
		if (CurrentNode->Allocated) {
			LEVEL_UP;
			continue;
		}

		// If GoDeeper is set and we are not split, we should split
		if (!CurrentNode->Split && GoDeeper) {
			CurrentNode->Split = 1;
			if (!CurrentNode->Right) {
				CurrentNode->Right = CreateNode(CurrentNode);
				CurrentNode->Left  = CreateNode(CurrentNode);
			}
		}

		// Assumptions past this point:
		// Since we are not marked as allocated, either one of our nodes are free. This is guarantee
		// made by the algorithm. We can also assume past this point that both left/right nodes exists
		// if GoDeeper is set
		if (GoDeeper) {
			if (!CurrentNode->Right->Allocated) {
				GO_RIGHT;
			}
			else {
				AccumulatedLength += NextLength;
				GO_LEFT;
			}
			continue;
		}
		else {
			if (CurrentNode->Split) {
				ParentNode = FindNextParent(Pool, CurrentNode, &Depth, &AccumulatedLength);
				if (ParentNode) {
					CurrentNode = ParentNode;
					continue;
				}
			}
			else {
				CurrentNode->Allocated = 1;
				Result = AccumulatedLength;
			}
			
			Finish = 1;
			LEVEL_UP;
		}
	}
	return Result;
}

uintptr_t
DynamicMemoryPoolAllocate(
	_In_ DynamicMemoryPool_t* Pool,
	_In_ size_t               Length)
{
	uintptr_t Result;
	assert(Pool != NULL);

    SpinlockAcquireIrq(&Pool->SyncObject);
	Result = IterativeAllocate(Pool, Length);
    SpinlockReleaseIrq(&Pool->SyncObject);
	
	TRACE("[utils] [dyn_mem_pool] allocate length 0x%" PRIxIN " => 0x%" PRIxIN,
		Length, Result);
	return Result;
}


static int
IterativeFree(
	_In_ DynamicMemoryPool_t* Pool,
	_In_ uintptr_t            Address)
{
	DynamicMemoryChunk_t* CurrentNode        = Pool->Root;
	size_t                AccumulatedAddress = Pool->StartAddress;
	int                   Depth              = 0;
	int                   Result             = -1;
	int                   Finish             = 0;
	size_t                CurrentLength;
	uintptr_t             RightChildLimit;
	uintptr_t             NodeLimit;

	while (CurrentNode) {
		CurrentLength = Depth != 0 ? (Pool->Length >> Depth) : Pool->Length;

		// Perform bookkeeping and also make sure we iterate up the chain again
		if (Finish) {
			if (!Result) {
				// Bookkeeping task 1 - If both children are now free, mark our node as free as well.
				if (!CurrentNode->Left->Allocated && !CurrentNode->Right->Allocated) {
					CurrentNode->Allocated = 0;

					// Bookkeeping task 2 - If both children are free and no longer split, then we can also
					// perform un-split on this node
					if (!CurrentNode->Left->Split && !CurrentNode->Right->Split) {
						CurrentNode->Split = 0;
					}
				}
			}
			LEVEL_UP;
			continue;
		}

		if (CurrentNode->Split) {
			// We can safely assume both nodes are not null if the node is split
			RightChildLimit = AccumulatedAddress + (CurrentLength >> 1);
			if (Address < RightChildLimit) {
				GO_RIGHT;
			} else {
				AccumulatedAddress += CurrentLength >> 1;
				GO_LEFT;
			}
			continue;
		} else {
			if (CurrentNode->Allocated) {
				NodeLimit = AccumulatedAddress + CurrentLength;
				if (Address >= AccumulatedAddress && Address < NodeLimit) {
					CurrentNode->Allocated = 0;
					Result = 0;
				}
			}
			Finish = 1;
			LEVEL_UP;
		}
	}
	return Result;
}

void
DynamicMemoryPoolFree(
	_In_ DynamicMemoryPool_t* Pool,
	_In_ uintptr_t            Address)
{
	int Result;
	assert(Pool != NULL);
	
	TRACE("[utils] [dyn_mem_pool] free 0x%" PRIxIN, Address);

    SpinlockAcquireIrq(&Pool->SyncObject);
	Result = IterativeFree(Pool, Address);
    SpinlockReleaseIrq(&Pool->SyncObject);
	if (Result) {
		WARNING("[utils] [dyn_mem_pool] failed to free Address 0x%" PRIxIN, Address);
	}
}

// Returns 1 if contains
int
DynamicMemoryPoolContains(
    _In_ DynamicMemoryPool_t* Pool,
    _In_ uintptr_t            Address)
{
	assert(Pool != NULL);
	return (Address >= Pool->StartAddress && Address < (Pool->StartAddress + Pool->Length));
}
