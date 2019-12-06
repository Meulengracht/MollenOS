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
 * Datastructure (Static Memory Pool)
 * - Implementation of a static-non-allocation memory Pool as a binary-tree.
 */
#define __MODULE "MEMP"

#include <assert.h>
#include <debug.h>
#include <utils/static_memory_pool.h>
#include <string.h>

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
		ERROR("[st_mem_pool] length 0x%" PRIxIN " is not a power of two");
	}

	Pool->Chunks = (StaticMemoryChunk_t*)Storage;
	Pool->StartAddress = StartAddress;
	Pool->Length = Length;
	Pool->ChunkSize = ChunkSize;
	memset(Storage, 0, StaticMemoryPoolCalculateSize(Length, ChunkSize));
}

static uintptr_t
RecursiveAllocate(
    _In_ StaticMemoryPool_t* Pool,
    _In_ int                 Index,
    _In_ int                 Level,
    _In_ size_t              AccumulatedLength,
    _In_ size_t              Length)
{
	size_t CurrentLength    = Level != 0 ? (Pool->Length >> Level) : Pool->Length;
	size_t NextLength       = (Pool->Length >> (Level + 1));
	int    LeftChildIndex  = (Index * 2) + 1;
	int    RightChildIndex = (Index * 2) + 2;

	// If we are allocated, return immediately
	if (Pool->Chunks[Index].Allocated) {
		return 0;
	}

	// If we are split, we can't be allocated and thus we should move on directly, unless
	// our size is bigger than the next level, then we should go back up
	if (Pool->Chunks[Index].Split) {
		if (Length > NextLength) {
			return 0;
		}
	}
	else {
		// Don't move below ChunkSize, since we don't have enough node space for finer grained allocates
		if (CurrentLength == Pool->ChunkSize || (CurrentLength >= Length && Length > NextLength)) {
			Pool->Chunks[Index].Allocated = 1;
			return AccumulatedLength;
		}
		Pool->Chunks[Index].Split = 1;
	}

	uintptr_t Result = RecursiveAllocate(Pool, RightChildIndex, Level + 1, AccumulatedLength, Length);
	if (!Result) {
		AccumulatedLength += NextLength;
		Result = RecursiveAllocate(Pool, LeftChildIndex, Level + 1, AccumulatedLength, Length);
	}

	// Book-keeping task to increase allocation performance (search performance)
	// If both children are allocated, mark us as allocated
	if (Pool->Chunks[LeftChildIndex].Allocated && 
		Pool->Chunks[RightChildIndex].Allocated) {
		Pool->Chunks[Index].Allocated = 1;
	}
	return Result;
}

uintptr_t
StaticMemoryPoolAllocate(
    StaticMemoryPool_t* Pool,
    size_t              Length)
{
	assert(Pool != NULL);
	return RecursiveAllocate(Pool, 0, 0, Pool->StartAddress, Length);
}

static int
RecursiveFree(
    _In_ StaticMemoryPool_t* Pool,
    _In_ int                 Index,
    _In_ int                 Level,
    _In_ size_t              AccumulatedAddress,
    _In_ uintptr_t           Address)
{
	size_t CurrentLength    = Level != 0 ? (Pool->Length >> Level) : Pool->Length;
	int    LeftChildIndex  = (Index * 2) + 1;
	int    RightChildIndex = (Index * 2) + 2;
	int    Result            = -1;

	// If we are split, we can't be allocated, and thus we should immediately move on to the next node,
	// decide if we should move on the left or right node.
	if (Pool->Chunks[Index].Split) {
		uintptr_t RightChildAddressLimit = AccumulatedAddress + (CurrentLength >> 1);
		if (Address < RightChildAddressLimit) {
			Result = RecursiveFree(Pool, RightChildIndex, Level + 1, AccumulatedAddress, Address);
		}
		else {
			AccumulatedAddress += CurrentLength >> 1;
			Result = RecursiveFree(Pool, LeftChildIndex, Level + 1, AccumulatedAddress, Address);
		}

		// Book-keeping task 1 - If both children are now free, mark our node as free as well.
		if (!Pool->Chunks[LeftChildIndex].Allocated && !Pool->Chunks[RightChildIndex].Allocated) {
			Pool->Chunks[Index].Allocated = 0;

			// Book-keeping task 2 - If both children are free and no longer split, then we can also
			// perform un-split on this node
			if (!Pool->Chunks[LeftChildIndex].Split && !Pool->Chunks[RightChildIndex].Split) {
				Pool->Chunks[Index].Split = 0;
			}
		}
	}
	else if (Pool->Chunks[Index].Allocated) {
		uintptr_t NodeLimit = AccumulatedAddress + CurrentLength;
		if (Address >= AccumulatedAddress && Address < NodeLimit) {
			Pool->Chunks[Index].Allocated = 0;
			Result = 0;
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
	Result = RecursiveFree(Pool, 0, 0, Pool->StartAddress, Address);
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
