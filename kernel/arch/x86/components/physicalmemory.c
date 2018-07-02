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

#include <ds/blbitmap.h>
#include <arch.h>
#include <memory.h>
#include <multiboot.h>
#include <debug.h>
#include <assert.h>

// Global static storage for the memory manager
static BlockBitmap_t PhysicalMemory = { 0 };
static size_t BlockmapBytes 		= 0;

/* MmMemoryDebugPrint
 * This is a debug function for inspecting
 * the memory status, it spits out how many blocks are in use */
void
MmMemoryDebugPrint(void) {
	TRACE("Bitmap size: %u Bytes", BlockmapBytes);
	TRACE("Memory in use %u Bytes", PhysicalMemory.BlocksAllocated * PAGE_SIZE);
	TRACE("Block status %u/%u", PhysicalMemory.BlocksAllocated, PhysicalMemory.BlockCount);
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
		*BlocksTotal = PhysicalMemory.BlockCount;
	}

	// Update allocated
	if (BlocksAllocated != NULL) {
		*BlocksAllocated = PhysicalMemory.BlocksAllocated;
	}
	return OsSuccess;
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
	uintptr_t MemorySize;
	int i;

	assert(BootInformation != NULL);

    // Initialize
	RegionPointer = (BIOSMemoryRegion_t*)(uintptr_t)BootInformation->MemoryMapAddress;

	// The memory-high part is 64kb blocks 
	// whereas the memory-low part is bytes of memory
	MemorySize  = (BootInformation->MemoryHigh * 64 * 1024);
	MemorySize  += BootInformation->MemoryLow; // This is in kilobytes 
	assert((MemorySize / 1024 / 1024) >= 32);
	ConstructBlockmap(&PhysicalMemory, (void*)MEMORY_LOCATION_BITMAP, 
		BLOCKMAP_ALLRESERVED, 0, MemorySize, PAGE_SIZE);
	BlockmapBytes = GetBytesNeccessaryForBlockmap(0, MemorySize, PAGE_SIZE);

	// Free regions given to us by memory map
	for (i = 0; i < (int)BootInformation->MemoryMapLength; i++) {
		if (RegionPointer->Type == 1) {
			ReleaseBlockmapRegion(&PhysicalMemory, 
				(uintptr_t)RegionPointer->Address, (size_t)RegionPointer->Size);
        }
		RegionPointer++;
	}

    // Mark default regions in use and special regions
    //  0x0000              || Used for catching null-pointers
    //  0x4000 + 0x8000     || Used for memory region & Trampoline-code
    //  0x90000 - 0x9F000   || Kernel Stack
    //  0x100000 - KernelSize
    //  0x200000 - RamDiskSize
    //  0x300000 - ??       || Bitmap Space
    ReserveBlockmapRegion(&PhysicalMemory, 0,         				0x10000);
	ReserveBlockmapRegion(&PhysicalMemory, 0x90000,   				0xF000);
	ReserveBlockmapRegion(&PhysicalMemory, MEMORY_LOCATION_KERNEL,	BootInformation->KernelSize + PAGE_SIZE);
	ReserveBlockmapRegion(&PhysicalMemory, MEMORY_LOCATION_RAMDISK,	BootInformation->RamdiskSize + PAGE_SIZE);
	ReserveBlockmapRegion(&PhysicalMemory, MEMORY_LOCATION_BITMAP, 	(BlockmapBytes + PAGE_SIZE));

	// Debug initial stats
	MmMemoryDebugPrint();
	return OsSuccess;
}

/* MmPhysicalAllocateBlock
 * This is the primary function for allocating
 * physical memory pages, this takes an argument
 * <Mask> which determines where in memory the allocation is OK */
PhysicalAddress_t
MmPhysicalAllocateBlock(
    _In_ uintptr_t          Mask, 
    _In_ int                Count)
{
	return AllocateBlocksInBlockmap(&PhysicalMemory, Mask, Count * PAGE_SIZE);
}

/* MmPhysicalFreeBlock
 * This is the primary function for
 * freeing physical pages, but NEVER free physical
 * pages if they exist in someones mapping */
OsStatus_t
MmPhysicalFreeBlock(
    _In_ PhysicalAddress_t  Address)
{
	return ReleaseBlockmapRegion(&PhysicalMemory, Address & PAGE_MASK, PAGE_SIZE);
}
