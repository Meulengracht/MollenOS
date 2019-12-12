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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Datastructure (Dynamic memory pool)
 * - Implementation of a memory pool as a binary-tree.
 */
#define __MODULE "MEMP"

#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <utils/dynamic_memory_pool.h>
#include <string.h>

static DynamicMemoryChunk_t*
CreateNode(void)
{
	DynamicMemoryChunk_t* Node = kmalloc(sizeof(DynamicMemoryChunk_t));
	if (!Node) {
		return NULL;
	}

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

	IrqSpinlockConstruct(&Pool->SyncObject);
	Pool->StartAddress = StartAddress;
	Pool->Length       = Length;
	Pool->ChunkSize    = ChunkSize;
	Pool->Root         = CreateNode();
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

static uintptr_t 
RecursiveAllocate(
	_In_ DynamicMemoryPool_t*   Pool,
	_In_ DynamicMemoryChunk_t*  Node,
	_In_ int                    Depth,
	_In_ size_t                 AccumulatedLength,
	_In_ size_t                 Length)
{
	size_t    CurrentLength = Depth != 0 ? (Pool->Length >> Depth) : Pool->Length;
	size_t    NextLength    = (Pool->Length >> (Depth + 1));
	uintptr_t Result;

	// If we are allocated, return immediately
	if (Node->Allocated) {
		return 0;
	}

	// If we are Split, we can't be allocated and thus we should move on directly, unless
	// our size is bigger than the next depth, then we should go back up
	if (Node->Split) {
		if (Length > NextLength) {
			return 0;
		}
	}
	else {
		// Don't move below ChunkSize, since we don't have enough node space for finer grained allocates
		if (CurrentLength == Pool->ChunkSize || (CurrentLength >= Length && Length > NextLength)) {
			Node->Allocated = 1;
			return Pool->StartAddress + AccumulatedLength;
		}
		
		if (!Node->Split) {
			Node->Split = 1;
			if (!Node->Right) {
				Node->Right = CreateNode();
				Node->Left  = CreateNode();
				if (!Node->Right || !Node->Left) {
					return 0;
				}
			}
		}
	}

	Result = RecursiveAllocate(Pool, Node->Right, Depth + 1, AccumulatedLength, Length);
	if (!Result) {
		AccumulatedLength += NextLength;
		Result = RecursiveAllocate(Pool, Node->Left, Depth + 1, AccumulatedLength, Length);
	}

	// Book-keeping task to increase allocation performance (search performance)
	// If both children are allocated, mark us as allocated
	if (Node->Left->Allocated && Node->Right->Allocated) {
		Node->Allocated = 1;
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
	
	IrqSpinlockAcquire(&Pool->SyncObject);
	Result = RecursiveAllocate(Pool, Pool->Root, 0, 0, Length);
	IrqSpinlockRelease(&Pool->SyncObject);
	return Result;
}

static int
RecursiveFree(
	_In_ DynamicMemoryPool_t*  Pool,
	_In_ DynamicMemoryChunk_t* Node,
	_In_ int                   Depth,
	_In_ size_t                AccumulatedAddress,
	_In_ uintptr_t             Address)
{
	size_t CurrentLength = Depth != 0 ? (Pool->Length >> Depth) : Pool->Length;
	int    Result         = -1;

	// If we are split, we can't be allocated, and thus we should immediately move on to the next node,
	// decide if we should move on the left or right node.
	if (Node->Split) {
		// If a node is split, it's nodes are not null
		uintptr_t RightChildAddressLimit = AccumulatedAddress + (CurrentLength >> 1);
		if (Address < RightChildAddressLimit) {
			Result = RecursiveFree(Pool, Node->Right, Depth + 1, AccumulatedAddress, Address);
		}
		else {
			AccumulatedAddress += CurrentLength >> 1;
			Result = RecursiveFree(Pool, Node->Left, Depth + 1, AccumulatedAddress, Address);
		}

		// Book-keeping task 1 - If both children are now free, mark our node as free as well.
		if (!Node->Left->Allocated && !Node->Right->Allocated) {
			Node->Allocated = 0;

			// Book-keeping task 2 - If both children are free and no longer split, then we can also
			// perform un-split on this node
			if (!Node->Left->Split && !Node->Right->Split) {
				Node->Split = 0;
			}
		}
	}
	else if (Node->Allocated) {
		uintptr_t NodeLimit = AccumulatedAddress + CurrentLength;
		if (Address >= AccumulatedAddress && Address < NodeLimit) {
			Node->Allocated = 0;
			Result = 0;
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
	IrqSpinlockAcquire(&Pool->SyncObject);
	Result = RecursiveFree(Pool, Pool->Root, 0, Pool->StartAddress, Address);
	IrqSpinlockRelease(&Pool->SyncObject);
	if (Result) {
		WARNING("[utils] [dyn_mem_pool] failed to free Address 0x%x\n", Address);
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
