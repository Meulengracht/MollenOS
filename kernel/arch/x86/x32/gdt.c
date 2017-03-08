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
 * MollenOS x86-32 Descriptor Table
 * - Global Descriptor Table
 * - Task State Segment 
 */

/* Includes
 * - System */
#include <heap.h>
#include <gdt.h>

/* Includes
 * - Library */
#include <stddef.h>
#include <string.h>

/* Externs from assembly */
__EXTERN void TssInstall(int GdtIndex);

/* We have no memory allocation system 
 * in place yet, so uhm, allocate in place */
GdtEntry_t GdtDescriptors[GDT_MAX_DESCRIPTORS];
TssEntry_t *TssDescriptors[GDT_MAX_TSS];
TssEntry_t BootTss;
Gdt_t Gdtptr;
int GblGdtIndex = 0;

/* Helper for installing a new gdt descriptor into
 * the descriptor table, a memory base, memory limit
 * access flags and a grandularity must be provided to
 * configurate the segment */
void GdtInstallDescriptor(uint32_t Base, uint32_t Limit,
	uint8_t Access, uint8_t Grandularity)
{
	/* Null the entry */
	memset(&GdtDescriptors[GblGdtIndex], 0, sizeof(GdtEntry_t));

	/* Base Address */
	GdtDescriptors[GblGdtIndex].BaseLow = (uint16_t)(Base & 0xFFFF);
	GdtDescriptors[GblGdtIndex].BaseMid = (uint8_t)((Base >> 16) & 0xFF);
	GdtDescriptors[GblGdtIndex].BaseHigh = (uint8_t)((Base >> 24) & 0xFF);

	/* Limits */
	GdtDescriptors[GblGdtIndex].LimitLow = (uint16_t)(Limit & 0xFFFF);
	GdtDescriptors[GblGdtIndex].Flags = (uint8_t)((Limit >> 16) & 0x0F);

	/* Granularity */
	GdtDescriptors[GblGdtIndex].Flags |= (Grandularity & 0xF0);

	/* Access flags */
	GdtDescriptors[GblGdtIndex].Access = Access;

	/* Increase index */
	GblGdtIndex++;
}

/* Initialize the gdt table with the 5 default
 * descriptors for kernel/user mode data/code segments */
void GdtInitialize(void)
{
	/* Setup the base GDT */
	Gdtptr.Limit = (sizeof(GdtEntry_t) * GDT_MAX_DESCRIPTORS) - 1;
	Gdtptr.Base = (uint32_t)&GdtDescriptors[0];
	GblGdtIndex = 0;

	/* NULL Descriptor */
	GdtInstallDescriptor(0, 0, 0, 0);

	/* Kernel segments
	 * Kernel segments span the entire virtual
	 * address space from 0 -> 0xFFFFFFFF */
	//(MEMORY_SEGMENT_KERNEL_CODE_LIMIT - 1) / PAGE_SIZE
	GdtInstallDescriptor(0, (MEMORY_SEGMNET_RING3_CODE_LIMIT - 1) / PAGE_SIZE,
		GDT_RING0_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_KERNEL_DATA_LIMIT,
		GDT_RING0_DATA, GDT_GRANULARITY);

	/* Applications segments
	 * Application segments does not span entire address space
	 * but rather in their own subset */
	GdtInstallDescriptor(0, (MEMORY_SEGMNET_RING3_CODE_LIMIT - 1) / PAGE_SIZE,
		GDT_RING3_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING3_DATA_LIMIT,
		GDT_RING3_DATA, GDT_GRANULARITY);

	/* Driver segments
	 * Driver segments does not span entire address space
	 * but rather in their own subset */
	GdtInstallDescriptor(0, (MEMORY_SEGMNET_RING3_CODE_LIMIT - 1) / PAGE_SIZE,
		GDT_RING3_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING3_DATA_LIMIT,
		GDT_RING3_DATA, GDT_GRANULARITY);

	/* Shared segments
	 * Stack segment shared between drivers and applications
	 * which goes into the highest page-table */
	GdtInstallDescriptor(MEMORY_SEGMENT_STACK_BASE - __MASK, 
		((__MASK - 1) - MEMORY_SEGMENT_STACK_LIMIT) / PAGE_SIZE,
		GDT_ACCESS_WRITABLE | GDT_ACCESS_DOWN | GDT_ACCESS_PRIV3 
		| GDT_ACCESS_RESERVED | GDT_ACCESS_PRESENT, 
		GDT_FLAG_32BIT | GDT_FLAG_PAGES);

	/* Null task pointers */
	memset(&TssDescriptors, 0, sizeof(TssDescriptors));

	/* Reload GDT */
	GdtInstall();

	/* Install first TSS, for the boot core */
	GdtInstallTss(0, 1);
}

/* Helper for setting up a new task state segment for
 * the given cpu core, this should be done once per
 * core, and it will set default params for the TSS */
void GdtInstallTss(UUId_t Cpu, int Static)
{
	/* Variables */
	uint32_t tBase = 0;
	uint32_t tLimit = 0;
	int TssIndex = GblGdtIndex;

	/* Use the static or allocate one? */
	if (Static) {
		TssDescriptors[Cpu] = &BootTss;
	}
	else {
		TssDescriptors[Cpu] = (TssEntry_t*)kmalloc(sizeof(TssEntry_t));
	}

	/* Reset the memory of the descriptor */
	memset(TssDescriptors[Cpu], 0, sizeof(TssEntry_t));

	/* Set up the tss-stuff */
	tBase = (uint32_t)TssDescriptors[Cpu];
	tLimit = tBase + sizeof(TssEntry_t);

	/* Setup TSS initial ring0 stack information
	 * this will be filled out properly later by scheduler */
	TssDescriptors[Cpu]->Ss0 = GDT_KDATA_SEGMENT;
	TssDescriptors[Cpu]->Ss2 = GDT_STACK_SEGMENT + 0x03;
	
	/* Set initial segment information (Ring0) */
	TssDescriptors[Cpu]->Cs = GDT_KCODE_SEGMENT + 0x03;
	TssDescriptors[Cpu]->Ss = GDT_KDATA_SEGMENT + 0x03;
	TssDescriptors[Cpu]->Ds = GDT_KDATA_SEGMENT + 0x03;
	TssDescriptors[Cpu]->Es = GDT_KDATA_SEGMENT + 0x03;
	TssDescriptors[Cpu]->Fs = GDT_KDATA_SEGMENT + 0x03;
	TssDescriptors[Cpu]->Gs = GDT_KDATA_SEGMENT + 0x03;
	TssDescriptors[Cpu]->IoMapBase = (uint16_t)offsetof(TssEntry_t, IoMap[0]);

	/* Install TSS descriptor into table */
	GdtInstallDescriptor(tBase, tLimit, GDT_TSS_ENTRY, 0x00);

	/* Install into system */
	TssInstall(TssIndex);
}

/* Updates the kernel/interrupt stack for the current
 * cpu tss entry, this should be updated at each task-switch */
void TssUpdateStack(UUId_t Cpu, Addr_t Stack)
{
	TssDescriptors[Cpu]->Esp0 = Stack;
}

/* Updates the io-map for the current runinng task, should
 * be updated each time there is a task-switch to reflect
 * io-privs. Iomap given must be length GDT_IOMAP_SIZE */
void TssUpdateIo(UUId_t Cpu, uint8_t *IoMap)
{
	memcpy(&TssDescriptors[Cpu]->IoMap[0], IoMap, GDT_IOMAP_SIZE);
}

/* Enables the given port in the given io-map, also updates
 * the change into the current tss for the given cpu to 
 * reflect the port-ownership instantly */
void TssEnableIo(UUId_t Cpu, uint8_t *IoMap, uint16_t Port)
{
	TssDescriptors[Cpu]->IoMap[Port / 8] &= ~(1 << (Port % 8));
	IoMap[Port / 8] &= ~(1 << (Port % 8));
}

/* Disables the given port in the given io-map, also updates
 * the change into the current tss for the given cpu to 
 * reflect the port-ownership instantly */
void TssDisableIo(UUId_t Cpu, uint8_t *IoMap, uint16_t Port)
{    
	TssDescriptors[Cpu]->IoMap[Port / 8] |= (1 << (Port % 8));
	IoMap[Port / 8] |= (1 << (Port % 8));
}
