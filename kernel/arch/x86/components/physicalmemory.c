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
 * MollenOS x86 Physical Memory Manager
 * Todo: Incorperate real support for Mask
 */
#define __MODULE "PMEM"
#define __TRACE

/* Includes 
 * - System */
#include <arch.h>
#include <memory.h>
#include <multiboot.h>
#include <criticalsection.h>
#include <debug.h>

/* Includes 
 * - Library */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Globals 
 * This is primarily stats and 
 * information about the memory bitmap */
static CriticalSection_t MemoryLock;
static uintptr_t *MemoryBitmap  = NULL;
static size_t MemoryBitmapSize  = 0;
static size_t MemoryBlocks      = 0;
static size_t MemoryBlocksUsed  = 0;
static size_t MemorySize        = 0;

/* MmMemoryDebugPrint
 * This is a debug function for inspecting
 * the memory status, it spits out how many blocks are in use */
void
MmMemoryDebugPrint(void) {
	TRACE("Bitmap size: %u Bytes", MemoryBitmapSize);
	TRACE("Memory in use %u Bytes", MemoryBlocksUsed * PAGE_SIZE);
	TRACE("Block status %u/%u", MemoryBlocksUsed, MemoryBlocks);
}

/* MmPhysicalQuery
 * Queries information about current block status */
OsStatus_t
MmPhysicalQuery(
	_Out_Opt_ size_t *BlocksTotal, 
	_Out_Opt_ size_t *BlocksAllocated)
{
	// Update total
	if (BlocksTotal != NULL) {
		*BlocksTotal = MemoryBlocks;
	}

	// Update allocated
	if (BlocksAllocated != NULL) {
		*BlocksAllocated = MemoryBlocksUsed;
	}
	return OsSuccess;
}

/* This is an inline helper for 
 * allocating a bit in a bitmap 
 * make sure this is tested before and give
 * an error if its already allocated */
void MmMemoryMapSetBit(int Bit) {
	MemoryBitmap[Bit / __BITS] |= (1 << (Bit % __BITS));
}

/* This is an inline helper for 
 * freeing a bit in a bitmap 
 * make sure this is tested before and give
 * an error if it's not allocated */
void MmMemoryMapUnsetBit(int Bit) {
	MemoryBitmap[Bit / __BITS] &= ~(1 << (Bit % __BITS));
}

/* This is an inline helper for 
 * testing whether or not a bit is set, it returns
 * 1 if allocated, or 0 if free */
int MmMemoryMapTestBit(int Bit) {
	return (MemoryBitmap[Bit / __BITS] & (1 << (Bit % __BITS))) > 0 ? 1 : 0;
}

/* MmGetFreeMapBitLow
 * This function can be used to retrieve a page of memory below the MEMORY_LOW_THRESHOLD 
 * this is useful for devices that use DMA */
int MmGetFreeMapBitLow(int Count)
{
	// Variables
	int i, j, Result = -1;

	/* Start out by iterating the 
	 * different memory blocks, but always skip the first mem-block */
	for (i = 1; i < (8 * 16); i++)
	{
		/* Quick-check, if it's maxxed we can skip it 
		 * due to all being allocated */
		if (MemoryBitmap[i] != __MASK) {
			for (j = 0; j < __BITS; j++) {
				if (!(MemoryBitmap[i] & 1 << j)) {
					int Found = 1;
					for (int k = 0, c = j; k < Count && c < __BITS; k++, c++) {
						if (MemoryBitmap[i] & 1 << c) {
							Found = 0;
							break;
						}
					}
					if (Found == 1) {
						Result = (int)((i * __BITS) + j);
						break;
					}
				}
			}
		}

		/* Check for break 
		 * If result is found then we are done! */
		if (Result != -1)
			break;
	}
	return Result;
}

/* This function can be used to retrieve
 * a page of memory above the MEMORY_LOW_THRESHOLD 
 * this should probably be the standard alloc used */
int MmGetFreeMapBitHigh(int Count)
{
	/* Variables needed for iteration */
	int i, j, Result = -1;

	/* Start out by iterating the
	 * different memory blocks, but always skip
	 * the first mem-block */
	for (i = (8 * 16); i < (int)MemoryBlocks; i++)
	{
		/* Quick-check, if it's maxxed we can skip it
		 * due to all being allocated */
		if (MemoryBitmap[i] != __MASK) {
			for (j = 0; j < __BITS; j++) {
				if (!(MemoryBitmap[i] & 1 << j)) {
					int Found = 1;
					for (int k = 0, c = j; k < Count && c < __BITS; k++, c++) {
						if (MemoryBitmap[i] & 1 << c) {
							Found = 0;
							break;
						}
					}
					if (Found == 1) {
						Result = (int)((i * __BITS) + j);
						break;
					}
				}
			}
		}

		/* Check for break
		 * If result is found then we are done! */
		if (Result != -1)
			break;
	}
	return Result;
}

/* One of the two region functions
 * they are helpers in order to either free
 * or allocate a region of memory */
void MmFreeRegion(uintptr_t Base, size_t Size)
{
	/* Calculate the frame */
	int Frame = (int)(Base / PAGE_SIZE);
	size_t Count = (size_t)(Size / PAGE_SIZE);

	/* Iterate and free the frames in the 
	 * bitmap using our helper function */
	for (size_t i = Base; Count > 0; Count--, i += PAGE_SIZE) {
		MmMemoryMapUnsetBit(Frame++);

		/* Decrease allocated blocks */
		if (MemoryBlocksUsed != 0)
			MemoryBlocksUsed--;
	}
}

/* One of the two region functions
 * they are helpers in order to either free
 * or allocate a region of memory */
void MmAllocateRegion(uintptr_t Base, size_t Size)
{
	/* Calculate the frame */
	int Frame = (int)(Base / PAGE_SIZE);
	size_t Count = (size_t)(Size / PAGE_SIZE);

	for (size_t i = Base; (Count + 1) > 0; Count--, i += PAGE_SIZE){
		MmMemoryMapSetBit(Frame++);
		MemoryBlocksUsed++;
	}
}

/* MmPhyiscalInit
 * This is the physical memory manager initializor
 * It reads the multiboot memory descriptor(s), initialies
 * the bitmap and makes sure reserved regions are allocated */
OsStatus_t
MmPhyiscalInit(
	_In_ Multiboot_t *BootInformation)
{
	// Variables
	BIOSMemoryRegion_t *RegionPointer = NULL;
	int i;

	assert(BootInformation != NULL);

    // Initialize
	RegionPointer = (BIOSMemoryRegion_t*)(uintptr_t)BootInformation->MemoryMapAddress;

	// The memory-high part is 64kb blocks 
	// whereas the memory-low part is bytes of memory
	MemorySize  = (BootInformation->MemoryHigh * 64 * 1024);
	MemorySize  += BootInformation->MemoryLow; // This is in kilobytes 
	assert((MemorySize / 1024 / 1024) >= 32);
	
    MemoryBitmap = (uintptr_t*)MEMORY_LOCATION_BITMAP;
	MemoryBlocks = MemorySize / PAGE_SIZE;
	MemoryBlocksUsed = MemoryBlocks;
	MemoryBitmapSize = DIVUP(MemoryBlocks, 8); // 8 blocks per byte

	// Reset all blocks to in-use
	memset((void*)MemoryBitmap, 0xFFFFFFFF, MemoryBitmapSize);
	CriticalSectionConstruct(&MemoryLock, CRITICALSECTION_PLAIN);
	for (i = 0; i < (int)BootInformation->MemoryMapLength; i++) {
		if (RegionPointer->Type == 1) {
			MmFreeRegion((uintptr_t)RegionPointer->Address, (size_t)RegionPointer->Size);
        }
		RegionPointer++;
	}

    // Mark default regions in use and special regions
    //  0x0000              || Used for catching null-pointers
    //  0x4000 - 0x6000     || Used for memory region & Trampoline-code
    //  0x90000 - 0x9F000   || Kernel Stack
    //  0x100000 - KernelSize
    //  0x200000 - RamDiskSize
    //  0x300000 - ??       || Bitmap Space
	MmMemoryMapSetBit(0);
	MmMemoryMapSetBit(0x4000 / PAGE_SIZE); // What the hell is this used for
	MmMemoryMapSetBit(0x5000 / PAGE_SIZE); // Trampoline code area
	MmMemoryMapSetBit(0x9000 / PAGE_SIZE); // Memory map (not needed anymore??)
	MmMemoryMapSetBit(0xA000 / PAGE_SIZE); // Used for vbe controller region (not needed anymore??)
	MemoryBlocksUsed += 5;

	MmAllocateRegion(0x90000, 0xF000);
	MmAllocateRegion(MEMORY_LOCATION_KERNEL, BootInformation->KernelSize + PAGE_SIZE);
	MmAllocateRegion(MEMORY_LOCATION_RAMDISK, BootInformation->RamdiskSize + PAGE_SIZE);
	MmAllocateRegion(MEMORY_LOCATION_BITMAP, (MemoryBitmapSize + PAGE_SIZE));

	// Debug initial stats
	MmMemoryDebugPrint();
	return OsSuccess;
}

/* MmPhysicalFreeBlock
 * This is the primary function for
 * freeing physical pages, but NEVER free physical
 * pages if they exist in someones mapping */
OsStatus_t
MmPhysicalFreeBlock(
	_In_ PhysicalAddress_t Address)
{
	// Variables
	int Frame = (int)(Address / PAGE_SIZE);
	if (Address >= MemorySize) {
        FATAL(FATAL_SCOPE_KERNEL, 
            "Tried to free address that was higher than allowed (0x%x >= 0x%x)",
            Address, MemorySize);
    }

    // Enter critical section
	CriticalSectionEnter(&MemoryLock);
	if (MmMemoryMapTestBit(Frame) == 0) {
	    CriticalSectionLeave(&MemoryLock);
        return OsError;
    }

	// Free the bit and leave section
	MmMemoryMapUnsetBit(Frame);
	CriticalSectionLeave(&MemoryLock);
	if (MemoryBlocksUsed != 0) {
		MemoryBlocksUsed--;
    }

	// Done - no errors
	return OsSuccess;
}

/* MmPhysicalAllocateBlock
 * This is the primary function for allocating physical memory pages, this takes an argument
 * <Mask> which determines where in memory the allocation is OK */
PhysicalAddress_t
MmPhysicalAllocateBlock(
	_In_ uintptr_t  Mask, 
	_In_ int        Count)
{
	// Variables
	int Frame = -1;

	assert(Count > 0);
	
    // Calculate which allocation function to use with the given mask
	CriticalSectionEnter(&MemoryLock);
	if (Mask <= 0xFFFFFF) {
		Frame = MmGetFreeMapBitLow(Count);
	}
	else {
		Frame = MmGetFreeMapBitHigh(Count);
	}

	// Set bit allocated before we release the lock, but ONLY if 
	// the frame is valid 
	if (Frame != -1) {
		for (int i = 0; i < Count; i++) {
			MmMemoryMapSetBit(Frame + i);
		}
	}
    CriticalSectionLeave(&MemoryLock);
	
    assert(Frame != -1);
	MemoryBlocksUsed++;

	// Calculate actual address
	return (PhysicalAddress_t)(Frame * PAGE_SIZE);
}
