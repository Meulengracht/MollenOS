/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS x86-32 Physical Memory Manager
*/

/* Includes */
#include <Arch.h>
#include <Memory.h>
#include <Multiboot.h>
#include <Mutex.h>
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <Log.h>

/* Globals */
volatile Addr_t *MemoryBitmap = NULL;
volatile uint32_t MemoryBitmapSize = 0;
volatile uint32_t MemoryBlocks = 0;
volatile uint32_t MemoryBlocksUsed = 0;
volatile uint32_t MemorySize = 0;
Mutex_t MemoryLock;

/* Reserved Regions */
SysMemMapping_t SysMappings[32];

/* Helpers */
void MmMemoryMapSetBit(int Bit)
{
	MemoryBitmap[Bit / 32] |= (1 << (Bit % 32));
}

void MmMemoryMapUnsetBit(int Bit)
{
	MemoryBitmap[Bit / 32] &= ~(1 << (Bit % 32));
}

uint8_t MmMemoryMapTestBit(int Bit)
{
	uint32_t block = MemoryBitmap[Bit / 32];
	uint32_t index = (1 << (Bit % 32));

	if (block & index)
		return 1;
	else
		return 0;
}

/* Get a free bit in the bitmap 
 * at low memory < 1 mb */
int MmGetFreeMapBitLow(void)
{
	uint32_t i;
	int j;
	int rbit = -1;

	/* Find time! */
	for (i = 1; i < 8; i++)
	{
		if (MemoryBitmap[i] != 0xFFFFFFFF)
		{
			for (j = 0; j < 32; j++)
			{
				int bit = 1 << j;
				if (!(MemoryBitmap[i] & bit))
				{
					rbit = (int)(i * 4 * 8 + j);
					break;
				}
			}
		}

		/* Check for break */
		if (rbit != -1)
			break;
	}

	/* Return frame */
	return rbit;
}

/* Get a free bit in the bitmap */
/* at high memory > 1 mb */
int MmGetFreeMapBitHigh(void)
{
	uint32_t i, max = MemoryBlocks;
	int j;
	int rbit = -1;

	/* Find time! */
	for (i = 8; i < max; i++)
	{
		if (MemoryBitmap[i] != 0xFFFFFFFF)
		{
			for (j = 0; j < 32; j++)
			{
				int bit = 1 << j;

				/* Test it */
				if (!(MemoryBitmap[i] & bit))
				{
					rbit = (int)(i * 4 * 8 + j);
					break;
				}
			}
		}

		/* Check for break */
		if (rbit != -1)
			break;
	}

	return rbit;
}

/* Frees a region of memory */
void MmFreeRegion(Addr_t Base, size_t Size)
{
	int align = (int32_t)(Base / PAGE_SIZE);
	int32_t blocks = (int32_t)(Size / PAGE_SIZE);
	uint32_t i;

	/* Block freeing loop */
	for (i = Base; blocks > 0; blocks--, i += PAGE_SIZE)
	{
		/* Free memory */
		MmMemoryMapUnsetBit(align++);

		/* Sanity */
		if (MemoryBlocksUsed != 0)
			MemoryBlocksUsed--;
	}
}

/* Allocate a region of memory */
void MmAllocateRegion(Addr_t Base, size_t Size)
{
	int align = (int32_t)(Base / PAGE_SIZE);
	int32_t blocks = (int32_t)(Size / PAGE_SIZE);
	uint32_t i;

	for (i = Base; (blocks + 1) > 0; blocks--, i += PAGE_SIZE)
	{
		/* Allocate memory */
		MmMemoryMapSetBit(align++);
		MemoryBlocksUsed++;
	}
}

/* Mappings contains type and address already? */
int MmSysMappingsContain(Addr_t Base, int Type)
{
	uint32_t i;

	/* Find address, if it exists! */
	for (i = 0; i < 32; i++)
	{
		if (SysMappings[i].Length == 0)
			continue;

		/* Does type match ? */
		if (SysMappings[i].Type == Type)
		{
			/* check if addr is matching */
			if (SysMappings[i].PhysicalAddrStart == Base)
				return 1;
		}
	}

	return 0;
}

/* Initialises the physical memory bitmap */
void MmPhyiscalInit(void *BootInfo, size_t KernelSize, size_t RamDiskSize)
{
	/* Step 1. Set location of memory bitmap at 2mb */
	Multiboot_t *mboot = (Multiboot_t*)BootInfo;
	MBootMemoryRegion_t *region = (MBootMemoryRegion_t*)mboot->MemoryMapAddr;
	uint32_t i, j;

	/* Right now they are not used */
	_CRT_UNUSED(KernelSize);
	_CRT_UNUSED(RamDiskSize);
	
	/* Get information from multiboot struct */
	MemorySize = mboot->MemoryHigh; /* This is how many blocks of 64 kb above 1 mb */
	MemorySize += mboot->MemoryLow; /* This is in kilobytes ... */
	MemorySize *= 1024;

	/* Sanity, we need AT LEAST 2 mb to run! */
	assert((MemorySize / 1024 / 1024) >= 4);

	/* Set storage variables */
	MemoryBitmap = (Addr_t*)MEMORY_LOCATION_BITMAP;
	MemoryBlocks = MemorySize / PAGE_SIZE;
	MemoryBlocksUsed = MemoryBlocks + 1;
	MemoryBitmapSize = (MemoryBlocks + 1) / 8; /* 8 blocks per byte, 32 per int */

	if ((MemoryBlocks + 1) % 8)
		MemoryBitmapSize++;

	/* Set all memory in use */
	memset((void*)MemoryBitmap, 0xF, MemoryBitmapSize);
	memset((void*)SysMappings, 0, sizeof(SysMappings));

	/* Reset Mutex */
	MutexConstruct(&MemoryLock);

	/* Let us make it possible to access 
	 * the first page of memory, but not through normal means */
	SysMappings[0].Type = 2;
	SysMappings[0].PhysicalAddrStart = 0;
	SysMappings[0].VirtualAddrStart = 0;
	SysMappings[0].Length = PAGE_SIZE;

	/* Loop through memory regions from bootloader */
	for (i = 0, j = 1; i < mboot->MemoryMapLength; i++)
	{
		if (!MmSysMappingsContain((PhysAddr_t)region->Address, (int)region->Type))
		{
			/* Available Region? */
			if (region->Type == 1)
				MmFreeRegion((PhysAddr_t)region->Address, (size_t)region->Size);

			/*printf("      > Memory Region %u: Address: 0x%x, Size 0x%x\n",
				region->type, (physaddr_t)region->address, (size_t)region->size);*/

			SysMappings[j].Type = region->Type;
			SysMappings[j].PhysicalAddrStart = (PhysAddr_t)region->Address;
			SysMappings[j].VirtualAddrStart = 0;
			SysMappings[j].Length = (size_t)region->Size;

			/* Advance */
			j++;
		}
		
		/* Advance to next */
		region++;
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

	/* 0x100000 - KernelSize */
	MmAllocateRegion(MEMORY_LOCATION_KERNEL, 0x100000);

	/* 0x200000 - RamDiskSize */
	MmAllocateRegion(MEMORY_LOCATION_RAMDISK, 0x100000);

	/* 0x300000 - ?? || Bitmap Space */
	MmAllocateRegion(MEMORY_LOCATION_BITMAP, (MemoryBitmapSize + PAGE_SIZE));

	/* Debug */
	LogInformation("PMEM", "Bitmap size: %u Bytes", MemoryBitmapSize);
	LogInformation("PMEM", "Memory in use %u Bytes", MemoryBlocksUsed * PAGE_SIZE);
}

void MmPhysicalFreeBlock(PhysAddr_t Addr)
{
	/* Calculate Bit */
	int bit = (int32_t)(Addr / PAGE_SIZE);

	/* Sanity */
	if (Addr > MemorySize
		|| Addr < 0x200000)
		return;

	/* Lock Mutex */
	MutexLock(&MemoryLock);

	/* Sanity */
	assert(MmMemoryMapTestBit(bit) != 0);

	/* Free it */
	MmMemoryMapUnsetBit(bit);

	/* Release Spinlock */
	MutexUnlock(&MemoryLock);

	/* Statistics */
	if (MemoryBlocksUsed != 0)
		MemoryBlocksUsed--;
}

PhysAddr_t MmPhysicalAllocateBlock(void)
{
	/* Get free bit */
	int bit;

	/* Get mutex */
	MutexLock(&MemoryLock);

	bit = MmGetFreeMapBitHigh();

	/* Sanity */
	assert(bit != -1);

	/* Set it */
	MmMemoryMapSetBit(bit);

	/* Release Spinlock */
	MutexUnlock(&MemoryLock);

	/* Statistics */
	MemoryBlocksUsed++;

	return (PhysAddr_t)(bit * PAGE_SIZE);
}

PhysAddr_t MmPhysicalAllocateBlockDma(void)
{
	/* Get free bit */
	int bit;

	/* Get spinlock */
	MutexLock(&MemoryLock);

	bit = MmGetFreeMapBitLow();

	/* Sanity */
	assert(bit != -1);

	/* Set it */
	MmMemoryMapSetBit(bit);

	/* Release Spinlock */
	MutexUnlock(&MemoryLock);

	/* Statistics */
	MemoryBlocksUsed++;

	return (PhysAddr_t)(bit * PAGE_SIZE);
}

/* Get system region */
VirtAddr_t MmPhyiscalGetSysMappingVirtual(PhysAddr_t PhysicalAddr)
{
	uint32_t i;

	/* Find address, if it exists! */
	for (i = 0; i < 32; i++)
	{
		if (SysMappings[i].Length != 0 && SysMappings[i].Type != 1)
		{
			/* Get start and end */
			PhysAddr_t start = SysMappings[i].PhysicalAddrStart;
			PhysAddr_t end = SysMappings[i].PhysicalAddrStart + SysMappings[i].Length - 1;

			/* Is it in range? :) */
			if (PhysicalAddr >= start && PhysicalAddr <= end)
			{
				/* Yay, return virtual mapping! */
				return SysMappings[i].VirtualAddrStart + (PhysicalAddr - SysMappings[i].PhysicalAddrStart);
			}
		}
	}

	return 0;
}