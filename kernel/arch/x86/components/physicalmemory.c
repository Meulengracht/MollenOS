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

/* Includes 
 * - System */
#include <arch.h>
#include <memory.h>
#include <multiboot.h>
#include <log.h>

/* Includes 
 * - C-Library */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Globals 
 * This is primarily stats and 
 * information about the memory
 * bitmap */
uintptr_t *MemoryBitmap = NULL;
size_t MemoryBitmapSize = 0;
size_t MemoryBlocks = 0;
size_t MemoryBlocksUsed = 0;
size_t MemorySize = 0;

/* The spinlock that protects
 * the physical memory manager */
Spinlock_t MemoryLock;

/* Reserved Regions 
 * This primarily comes from the region-descriptor */
SystemMemoryMapping_t SysMappings[32];

/* MmMemoryDebugPrint
 * This is a debug function for inspecting
 * the memory status, it spits out how many blocks are in use */
void
MmMemoryDebugPrint(void)
{
	LogInformation("PMEM", "Bitmap size: %u Bytes", MemoryBitmapSize);
	LogInformation("PMEM", "Memory in use %u Bytes", MemoryBlocksUsed * PAGE_SIZE);
	LogInformation("PMEM", "Block status %u/%u", MemoryBlocksUsed, MemoryBlocks);
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

	// Never fails
	return OsNoError;
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

/* This function can be used to retrieve
 * a page of memory below the MEMORY_LOW_THRESHOLD 
 * this is useful for devices that use DMA */
int MmGetFreeMapBitLow(int Count)
{
	/* Variables needed for iteration */
	int i, j, Result = -1;

	/* Start out by iterating the 
	 * different memory blocks, but always skip
	 * the first mem-block */
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

	/* Return frame */
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

	/* Return frame */
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

/* This validates if a system mapping already
 * exists at the given <Physical> address and of
 * the given type */
int MmSysMappingsContain(uintptr_t Base, int Type)
{
	/* Find address, if it exists! */
	for (int i = 0; i < 32; i++)
	{
		/* Sanity, it has to be a valid mapping */
		if (SysMappings[i].Length == 0)
			continue;

		/* Does type/address match ? */
		if (SysMappings[i].Type == Type
			&& SysMappings[i].pAddressStart == Base) {
			return 1;
		}
	}

	return 0;
}

/* MmPhyiscalInit
 * This is the physical memory manager initializor
 * It reads the multiboot memory descriptor(s), initialies
 * the bitmap and makes sure reserved regions are allocated */
OsStatus_t
MmPhyiscalInit(
	_In_ void *BootInfo, 
	_In_ MCoreBootDescriptor *Descriptor)
{
	/* Variables, cast neccessary data */
	Multiboot_t *BootDesc = (Multiboot_t*)BootInfo;
	BIOSMemoryRegion_t *RegionItr = NULL;
	int i, j;
	
	/* Sanitize the bootdescriptor */
	assert(BootDesc != NULL);

	/* Good, good ! 
	 * Get a pointer to the region descriptors */
	RegionItr = (BIOSMemoryRegion_t*)BootDesc->MemoryMapAddress;

	/* Get information from multiboot struct 
	 * The memory-high part is 64kb blocks 
	 * whereas the memory-low part is bytes of memory */
	MemorySize = (BootDesc->MemoryHigh * 64 * 1024);
	MemorySize += BootDesc->MemoryLow; /* This is in kilobytes ... */

	/* Sanity, we need AT LEAST 32 mb to run! */
	assert((MemorySize / 1024 / 1024) >= 32);

	/* Set storage variables 
	 * We have the bitmap normally at 2mb mark */
	MemoryBitmap = (uintptr_t*)MEMORY_LOCATION_BITMAP;
	MemoryBlocks = MemorySize / PAGE_SIZE;
	MemoryBlocksUsed = MemoryBlocks;
	MemoryBitmapSize = DIVUP(MemoryBlocks, 8); /* 8 blocks per byte, 32/64 per int */

	/* Set all memory in use */
	memset((void*)MemoryBitmap, 0xFFFFFFFF, MemoryBitmapSize);
	memset((void*)SysMappings, 0, sizeof(SysMappings));

	/* Reset Spinlock */
	SpinlockReset(&MemoryLock);

	/* Let us make it possible to access 
	 * the first page of memory, but not through normal means */
	SysMappings[0].Type = 2;
	SysMappings[0].pAddressStart = 0;
	SysMappings[0].vAddressStart = 0;
	SysMappings[0].Length = PAGE_SIZE;

	/* Loop through memory regions from bootloader */
	for (i = 0, j = 1; i < (int)BootDesc->MemoryMapLength; i++) {
		if (!MmSysMappingsContain((PhysicalAddress_t)RegionItr->Address, (int)RegionItr->Type))
		{
			/* Available Region? 
			 * It has to be of type 1 */
			if (RegionItr->Type == 1)
				MmFreeRegion((uintptr_t)RegionItr->Address, (size_t)RegionItr->Size);

			/*printf("      > Memory Region %u: Address: 0x%x, Size 0x%x\n",
				region->type, (PhysicalAddress_t)region->address, (size_t)region->size);*/

			/* Setup a new system mapping, 
			 * we cache this map for conveniance */
			SysMappings[j].Type = RegionItr->Type;
			SysMappings[j].pAddressStart = (PhysicalAddress_t)RegionItr->Address;
			SysMappings[j].vAddressStart = 0;
			SysMappings[j].Length = (size_t)RegionItr->Size;

			/* Advance */
			j++;
		}
		
		/* Advance to next */
		RegionItr++;
	}

	/* Mark special regions as reserved */
	MmMemoryMapSetBit(0);

	/* 0x4000 - 0x6000 || Used for memory region & Trampoline-code */
	MmMemoryMapSetBit(0x4000 / PAGE_SIZE);
	MmMemoryMapSetBit(0x5000 / PAGE_SIZE);
	MmMemoryMapSetBit(0x9000 / PAGE_SIZE);
	MmMemoryMapSetBit(0xA000 / PAGE_SIZE);
	MemoryBlocksUsed += 4;

	/* 0x90000 - 0x9F000 || Kernel Stack */
	MmAllocateRegion(0x90000, 0xF000);

	/* 0x100000 - 0x200000 
	 * Untill we know how much the kernel itself actually takes up 
	 * after PE relocation */
	MmAllocateRegion(MEMORY_LOCATION_KERNEL, Descriptor->KernelSize + PAGE_SIZE);

	/* 0x200000 - RamDiskSize */
	MmAllocateRegion(MEMORY_LOCATION_RAMDISK, Descriptor->RamDiskSize + PAGE_SIZE);

	/* 0x300000 - ?? || Bitmap Space 
	 * We allocate an extra guard-page */
	MmAllocateRegion(MEMORY_LOCATION_BITMAP, (MemoryBitmapSize + PAGE_SIZE));

	/* Debug */
	MmMemoryDebugPrint();

	// No problems
	return OsNoError;
}

/* MmPhysicalFreeBlock
 * This is the primary function for
 * freeing physical pages, but NEVER free physical
 * pages if they exist in someones mapping */
OsStatus_t
MmPhysicalFreeBlock(
	_In_ PhysicalAddress_t Address)
{
	/* Calculate the bitmap bit */
	int Frame = (int)(Address / PAGE_SIZE);

	/* Sanitize the address
	 * parameter for ranges */
	assert(Address < MemorySize);

	/* Get Spinlock */
	SpinlockAcquire(&MemoryLock);

	/* Sanitize that the page is 
	 * actually allocated */
	assert(MmMemoryMapTestBit(Frame) != 0);

	/* Free it */
	MmMemoryMapUnsetBit(Frame);

	/* Release Spinlock */
	SpinlockRelease(&MemoryLock);

	/* Statistics */
	if (MemoryBlocksUsed != 0)
		MemoryBlocksUsed--;

	// Done - no errors
	return OsNoError;
}

/* MmPhysicalAllocateBlock
 * This is the primary function for allocating
 * physical memory pages, this takes an argument
 * <Mask> which determines where in memory the allocation is OK */
PhysicalAddress_t
MmPhysicalAllocateBlock(
	_In_ uintptr_t Mask, 
	_In_ int Count)
{
	/* Variables, keep track of 
	 * the frame allocated */
	int Frame = -1;

	/* Sanitize params */
	assert(Count > 0);

	/* Get Spinlock */
	SpinlockAcquire(&MemoryLock);

	/* Calculate which allocation function
	 * to use with the given mask */
	if (Mask <= 0xFFFFFF) {
		Frame = MmGetFreeMapBitLow(Count);
	}
	else {
		Frame = MmGetFreeMapBitHigh(Count);
	}

	/* Set bit allocated before we 
	 * release the lock, but ONLY if 
	 * the frame is valid */
	if (Frame != -1) {
		for (int i = 0; i < Count; i++) {
			MmMemoryMapSetBit(Frame + i);
		}
	}

	/* Release lock */
	SpinlockRelease(&MemoryLock);

	/* Sanity */
	assert(Frame != -1);

	/* Statistics */
	MemoryBlocksUsed++;

	/* Calculate the return 
	 * address by multiplying by block size */
	return (PhysicalAddress_t)(Frame * PAGE_SIZE);
}

/* MmPhyiscalGetSysMappingVirtual
 * This function retrieves the virtual address 
 * of an mapped system mapping, this is to avoid
 * re-mapping and continous unmap of device memory 
 * Returns 0 if none exists */
VirtualAddress_t
MmPhyiscalGetSysMappingVirtual(
	_In_ PhysicalAddress_t PhysicalAddress)
{
	/* Iterate the sys-mappings, we only
	 * have up to 32 at the moment, should always be enough */
	for (int i = 0; i < 32; i++) {
		/* It has to be valid, and NOT of type available */
		if (SysMappings[i].Length != 0 && SysMappings[i].Type != 1) 
		{
			/* Calculate start and end 
			 * of this system memory region */
			PhysicalAddress_t Start = SysMappings[i].pAddressStart;
			PhysicalAddress_t End = SysMappings[i].pAddressStart + SysMappings[i].Length;

			/* Is it in range? :) */
			if (PhysicalAddress >= Start && PhysicalAddress < End) {
				return SysMappings[i].vAddressStart 
					+ (PhysicalAddress - SysMappings[i].pAddressStart);
			}
		}
	}

	/* Not found */
	return 0;
}
