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
 * Datastructure (Static Memory Pool)
 * - Implementation of a static-non-allocation memory Pool as a binary-tree.
 */
#define __MODULE "static_pool"

#include <assert.h>
#include <debug.h>
#include <utils/static_memory_pool.h>
#include <string.h>

#define LEVEL_UP   Index = ParentIndex; Depth--
#define GO_LEFT    Index = LeftChildIndex; Depth++
#define GO_RIGHT   Index = RightChildIndex; Depth++
#define CURRENT    Pool->Chunks[Index]
#define LEFT       Pool->Chunks[LeftChildIndex]
#define RIGHT      Pool->Chunks[RightChildIndex]

static int
mem_pow(
    _In_ int x,
    _In_ int n)
{
	int value = 1;
	int i;
	
	for (i = 0; i < n; ++i) {
		value *= x;
	}
	return value;
}

size_t
StaticMemoryPoolCalculateSize(
    _In_ size_t Length,
    _In_ size_t ChunkSize)
{
	int BottomNodeCount = Length / ChunkSize;
	int NodeCount;
	int Levels = 0;
	
	while (BottomNodeCount) {
		Levels++;
		BottomNodeCount >>= 1;
	}

	NodeCount = mem_pow(2, Levels) - 1;
	return NodeCount * sizeof(StaticMemoryChunk_t);
}

void
StaticMemoryPoolConstruct(
    _In_ StaticMemoryPool_t* Pool,
    _In_ void*               Storage,
    _In_ uintptr_t           StartAddress,
    _In_ size_t              Length,
    _In_ size_t              ChunkSize)
{
	assert(Pool != NULL);
	assert(Storage != NULL);
	
	if (!IsPowerOfTwo(Length)) {
		ERROR("[utils] [st_mem_pool] length 0x%" PRIxIN " is not a power of two");
	}

    SpinlockConstruct(&Pool->SyncObject);
	Pool->Chunks = (StaticMemoryChunk_t*)Storage;
	Pool->StartAddress = StartAddress;
	Pool->Length = Length;
	Pool->ChunkSize = ChunkSize;
	memset(Storage, 0, StaticMemoryPoolCalculateSize(Length, ChunkSize));
}


void
StaticMemoryPoolRelocate(
        _In_ StaticMemoryPool_t* Pool,
        _In_ void*               Storage)
{
    assert(Pool != NULL);

    Pool->Chunks = (StaticMemoryChunk_t*)Storage;
}

static int
FindNextParent(
	_In_  StaticMemoryPool_t* Pool,
	_In_  int                 Index,
	_Out_ int*                Depth,
	_Out_ size_t*             AccumulatedLength)
{
	int    FinderIndex  = Index;
	int    FinderDepth  = *Depth;
	size_t FinderLength = *AccumulatedLength;

	while (FinderIndex != -1) {
		int ParentIndex = FinderIndex == 0 ? -1 : (FinderIndex - 1) >> 1;

		if (ParentIndex != -1) {
			int ParentLeftChildIndex = (ParentIndex * 2) + 1;

			if (!Pool->Chunks[ParentLeftChildIndex].Allocated && 
					ParentLeftChildIndex != FinderIndex) {
				FinderLength += (Pool->Length >> FinderDepth);
				*Depth = FinderDepth;
				*AccumulatedLength = FinderLength;
				return ParentLeftChildIndex;
			}

			// adjust metrics
			if (ParentLeftChildIndex == FinderIndex) {
				FinderLength -= (Pool->Length >> FinderDepth);
			}
			FinderDepth--;
		}
		FinderIndex = ParentIndex;
	}
	return -1;
}

static uintptr_t
IterativeAllocate(
	_In_ StaticMemoryPool_t* Pool,
	_In_ size_t              Length)
{
	size_t    AccumulatedLength = Pool->StartAddress;
	uintptr_t Result = 0;
	int       Index  = 0;
	int       Finish = 0;
	int       Depth  = 0;

	while (Index != -1) {
		int    ParentIndex     = Index == 0 ? -1 : (Index - 1) >> 1;
		int    LeftChildIndex  = (Index * 2) + 1;
		int    RightChildIndex = (Index * 2) + 2;
		size_t CurrentLength   = Depth != 0 ? (Pool->Length >> Depth) : Pool->Length;
		size_t NextLength      = (Pool->Length >> (Depth + 1));
		int    GoDeeper        = (Length <= NextLength && CurrentLength != Pool->ChunkSize);
		
		// Have we already made the allocation? And now we are performing bookkeeping?
		if (Finish) {
			if (Result) {
				// Book-keeping task to increase allocation performance (search performance)
				// If both children are allocated, mark us as allocated
				if (LEFT.Allocated && RIGHT.Allocated) {
					CURRENT.Allocated = 1;
				}
			}
			LEVEL_UP;
			continue;
		}

		// Go back up a level if this node is allocated
		if (CURRENT.Allocated) {
			LEVEL_UP;
			continue;
		}

		if (GoDeeper) {
			CURRENT.Split = 1;
			if (!RIGHT.Allocated) {
				GO_RIGHT;
			}
			else {
				AccumulatedLength += NextLength;
				GO_LEFT;
			}
			continue;
		}
		else {
			if (CURRENT.Split) {
				int NewParentIndex = FindNextParent(Pool, Index, &Depth, &AccumulatedLength);
				if (NewParentIndex != -1) {
					Index = NewParentIndex;
					continue;
				}
			}
			else {
				CURRENT.Allocated = 1;
				Result = AccumulatedLength;
			}

			Finish = 1;
			LEVEL_UP;
		}
	}
	return Result;
}

uintptr_t
StaticMemoryPoolAllocate(
    StaticMemoryPool_t* Pool,
    size_t              Length)
{
	uintptr_t Result;
	assert(Pool != NULL);

    SpinlockAcquireIrq(&Pool->SyncObject);
	Result = IterativeAllocate(Pool, Length);
    SpinlockReleaseIrq(&Pool->SyncObject);
	return Result;
}

static int
IterativeFree(
	_In_ StaticMemoryPool_t* Pool,
	_In_ uintptr_t           Address)
{
	size_t AccumulatedAddress = Pool->StartAddress;
	int    Index              = 0;
	int    Depth              = 0;
	int    Result             = -1;
	int    Finish             = 0;

	while (Index != -1) {
		int    ParentIndex     = Index == 0 ? -1 : (Index - 1) >> 1;
		int    LeftChildIndex  = (Index * 2) + 1;
		int    RightChildIndex = (Index * 2) + 2;
		size_t CurrentLength   = Depth != 0 ? (Pool->Length >> Depth) : Pool->Length;

		// Perform book-keeping and also make sure we iterate up the chain again
		if (Finish) {
			if (!Result) {
				// Book-keeping task 1 - If both children are now free, mark our node as free as well.
				if (!LEFT.Allocated && !RIGHT.Allocated) {
					CURRENT.Allocated = 0;

					// Book-keeping task 2 - If both children are free and no longer split, then we can also
					// perform un-split on this node
					if (!LEFT.Split && !RIGHT.Split) {
						CURRENT.Split = 0;
					}
				}
			}
			LEVEL_UP;
			continue;
		}

		if (CURRENT.Split) {
			// We can safely assume both nodes are not null if the node is split
			uintptr_t RightChildAddressLimit = AccumulatedAddress + (CurrentLength >> 1);
			if (Address < RightChildAddressLimit) {
				GO_RIGHT;
			}
			else {
				AccumulatedAddress += CurrentLength >> 1;
				GO_LEFT;
			}
			continue;
		}
		else {
			if (CURRENT.Allocated) {
				uintptr_t NodeLimit = AccumulatedAddress + CurrentLength;
				if (Address >= AccumulatedAddress && Address < NodeLimit) {
					CURRENT.Allocated = 0;
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
StaticMemoryPoolFree(
    _In_ StaticMemoryPool_t* Pool,
    _In_ uintptr_t           Address)
{
	int Result;
	assert(Pool != NULL);

    SpinlockAcquireIrq(&Pool->SyncObject);
	Result = IterativeFree(Pool, Address);
    SpinlockReleaseIrq(&Pool->SyncObject);
	if (Result) {
		WARNING("[memory_pool_free] failed to free address 0x%x\n", Address);
	}
}

// Returns 1 if contains
int
StaticMemoryPoolContains(
    _In_ StaticMemoryPool_t* Pool,
    _In_ uintptr_t           Address)
{
	assert(Pool != NULL);
	return (Address >= Pool->StartAddress && Address < (Pool->StartAddress + Pool->Length));
}
