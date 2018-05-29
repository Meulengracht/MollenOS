/* MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
#include <system/utils.h>
#include <memory.h>
#include <arch.h>
#include <heap.h>
#include <gdt.h>

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <string.h>

/* Externs
 * Provide access to some assembler functions */
__EXTERN void TssInstall(int GdtIndex);

/* Globals
 * Static storage as we have no memory allocator here */
GdtObject_t __GdtTableObject; // Don't make static, used in asm
static GdtDescriptor_t Descriptors[GDT_MAX_DESCRIPTORS]    = { { 0 } };
static TssDescriptor_t *TssPointers[GDT_MAX_TSS]           = { 0 };
static TssDescriptor_t BootTss                             = { 0 };
static _Atomic(int) GdtIndicer                             = ATOMIC_VAR_INIT(0);

/* GdtInstallDescriptor
 * Helper for installing a new gdt descriptor into
 * the descriptor table, a memory base, memory limit
 * access flags and a grandularity must be provided to
 * configurate the segment */
int
GdtInstallDescriptor(
    _In_ uint32_t       Base, 
    _In_ uint32_t       Limit,
    _In_ uint8_t        Access, 
    _In_ uint8_t        Grandularity)
{
    int GdtIndex = atomic_fetch_add(&GdtIndicer, 1);

	// Fill descriptor
	Descriptors[GdtIndex].BaseLow       = (uint16_t)(Base & 0xFFFF);
	Descriptors[GdtIndex].BaseMid       = (uint8_t)((Base >> 16) & 0xFF);
	Descriptors[GdtIndex].BaseHigh      = (uint8_t)((Base >> 24) & 0xFF);
	Descriptors[GdtIndex].LimitLow      = (uint16_t)(Limit & 0xFFFF);
	Descriptors[GdtIndex].Flags         = (uint8_t)((Limit >> 16) & 0x0F);
	Descriptors[GdtIndex].Flags         |= (Grandularity & 0xF0);
	Descriptors[GdtIndex].Access        = Access;
    return GdtIndex;
}

/* GdtInitialize
 * Initialize the gdt table with the 5 default
 * descriptors for kernel/user mode data/code segments */
void
GdtInitialize(void)
{
	// Setup gdt-table object
	__GdtTableObject.Limit  = (sizeof(GdtDescriptor_t) * GDT_MAX_DESCRIPTORS) - 1;
	__GdtTableObject.Base   = (uint32_t)&Descriptors[0];

	// Install NULL descriptor
	GdtInstallDescriptor(0, 0, 0, 0);

	// Kernel segments
	// Kernel segments span the entire virtual
	// address space from 0 -> 0xFFFFFFFF
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING0_LIMIT / PAGE_SIZE,
		GDT_RING0_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING0_LIMIT,
		GDT_RING0_DATA, GDT_GRANULARITY);

	// Applications segments
	// Application segments does not span entire address space
	// but rather in their own subset
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING3_LIMIT / PAGE_SIZE,
		GDT_RING3_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING3_LIMIT,
		GDT_RING3_DATA, GDT_GRANULARITY);

	// Driver segments
	// Driver segments does not span entire address space
	// but rather in their own subset
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING3_LIMIT / PAGE_SIZE,
		GDT_RING3_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING3_LIMIT,
		GDT_RING3_DATA, GDT_GRANULARITY);

	// Shared segments
	// Extra segment shared between drivers and applications
	// which goes into the highest page-table
	GdtInstallDescriptor(MEMORY_SEGMENT_EXTRA_BASE, 
		(MEMORY_SEGMENT_EXTRA_SIZE - 1) / PAGE_SIZE,
		GDT_RING3_DATA, GDT_GRANULARITY);

	// Prepare gdt and tss for boot cpu
	GdtInstall();
	TssInitialize(1);
}

/* TssInitialize
 * Helper for setting up a new task state segment for
 * the given cpu core, this should be done once per
 * core, and it will set default params for the TSS */
void
TssInitialize(
    _In_ int    PrimaryCore)
{
	// Variables
	uint32_t tBase  = 0;
	uint32_t tLimit = 0;
    UUId_t CoreId   = CpuGetCurrentId();

	// If we use the static allocator, it must be the boot cpu
	if (PrimaryCore) {
		TssPointers[CoreId] = &BootTss;
	}
	else {
		TssPointers[CoreId] = (TssDescriptor_t*)kmalloc(sizeof(TssDescriptor_t));
	}

	// Initialize descriptor by zeroing and set default members
	memset(TssPointers[CoreId], 0, sizeof(TssDescriptor_t));
	tBase   = (uint32_t)TssPointers[CoreId];
	tLimit  = tBase + sizeof(TssDescriptor_t);

	// Setup TSS initial ring0 stack information
	// this will be filled out properly later by scheduler
	TssPointers[CoreId]->Ss0        = GDT_KDATA_SEGMENT;
	TssPointers[CoreId]->Ss2        = GDT_RING3_DATA + 0x03;
	
	// Set initial segment information (Ring0)
	TssPointers[CoreId]->Cs         = GDT_KCODE_SEGMENT + 0x03;
	TssPointers[CoreId]->Ss         = GDT_KDATA_SEGMENT + 0x03;
	TssPointers[CoreId]->Ds         = GDT_KDATA_SEGMENT + 0x03;
	TssPointers[CoreId]->Es         = GDT_KDATA_SEGMENT + 0x03;
	TssPointers[CoreId]->Fs         = GDT_KDATA_SEGMENT + 0x03;
	TssPointers[CoreId]->Gs         = GDT_KDATA_SEGMENT + 0x03;
    TssPointers[CoreId]->IoMapBase  = (uint16_t)offsetof(TssDescriptor_t, IoMap[0]);

	// Install TSS into table and hardware
	TssInstall(GdtInstallDescriptor(tBase, tLimit, GDT_TSS_ENTRY, 0x00));
}

/* TssUpdateThreadStack
 * Updates the kernel/interrupt stack for the current
 * cpu tss entry, this should be updated at each task-switch */
void
TssUpdateThreadStack(
    _In_ UUId_t     Cpu, 
    _In_ uintptr_t  Stack)
{
    assert(TssPointers[Cpu] != NULL);
	TssPointers[Cpu]->Esp0 = Stack;
}

/* TssUpdateIo
 * Updates the io-map for the current runinng task, should
 * be updated each time there is a task-switch to reflect
 * io-privs. Iomap given must be length GDT_IOMAP_SIZE */
void
TssUpdateIo(
    _In_ UUId_t     Cpu,
    _In_ uint8_t*   IoMap) {
    assert(TssPointers[Cpu] != NULL);
	memcpy(&TssPointers[Cpu]->IoMap[0], IoMap, GDT_IOMAP_SIZE);
}

/* TssEnableIo
 * Enables the given port in the given io-map, also updates
 * the change into the current tss for the given cpu to 
 * reflect the port-ownership instantly */
void
TssEnableIo(
    _In_ UUId_t     Cpu,
    _In_ uint16_t   Port) {
    assert(TssPointers[Cpu] != NULL);
	TssPointers[Cpu]->IoMap[Port / 8] &= ~(1 << (Port % 8));
}

/* TssDisableIo
 * Disables the given port in the given io-map, also updates
 * the change into the current tss for the given cpu to 
 * reflect the port-ownership instantly */
void
TssDisableIo(
    _In_ UUId_t     Cpu,
    _In_ uint16_t   Port) {
    assert(TssPointers[Cpu] != NULL);
	TssPointers[Cpu]->IoMap[Port / 8] |= (1 << (Port % 8));
}
