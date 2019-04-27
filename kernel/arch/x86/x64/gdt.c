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
 * x86-64 Descriptor Table
 * - Global Descriptor Table
 * - Task State Segment 
 */

#include <arch/utils.h>
#include <interrupts.h>
#include <memory.h>
#include <assert.h>
#include <string.h>
#include <arch.h>
#include <heap.h>
#include <gdt.h>

extern void TssInstall(int GdtIndex);

GdtObject_t __GdtTableObject; // Don't make static, used in asm
static GdtDescriptor_t Descriptors[GDT_MAX_DESCRIPTORS] = { { 0 } };
static TssDescriptor_t *TssPointers[GDT_MAX_TSS]        = { 0 };
static TssDescriptor_t BootTss                          = { 0 };
static _Atomic(int) GdtIndicer                          = ATOMIC_VAR_INIT(0);

static int
GdtInstallDescriptor(
    _In_ uint64_t Base, 
    _In_ uint32_t Limit,
    _In_ uint8_t  Access, 
    _In_ uint8_t  Grandularity)
{
    int GdtIndex = atomic_fetch_add(&GdtIndicer, 1);

	// Fill descriptor
	Descriptors[GdtIndex].BaseLow   = (uint16_t)(Base & 0xFFFF);
	Descriptors[GdtIndex].BaseMid   = (uint8_t)((Base >> 16) & 0xFF);
	Descriptors[GdtIndex].BaseHigh  = (uint8_t)((Base >> 24) & 0xFF);
	Descriptors[GdtIndex].BaseUpper = (uint32_t)((Base >> 32) & 0xFFFFFFFF);
	Descriptors[GdtIndex].LimitLow  = (uint16_t)(Limit & 0xFFFF);
	Descriptors[GdtIndex].Flags     = (uint8_t)((Limit >> 16) & 0x0F);
	Descriptors[GdtIndex].Flags     |= (Grandularity & 0xF0);
	Descriptors[GdtIndex].Access    = Access;
    Descriptors[GdtIndex].Reserved  = 0;
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
	__GdtTableObject.Base   = (uint64_t)&Descriptors[0];

	// Install NULL descriptor
	GdtInstallDescriptor(0, 0, 0, 0);

	// Kernel segments
	// Kernel segments span the entire virtual
	// address space from 0 -> 0xFFFFFFFF
	GdtInstallDescriptor(0, 0, GDT_RING0_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, 0, GDT_RING0_DATA, GDT_GRANULARITY);

	// Applications segments
	// Application segments does not span entire address space
	// but rather in their own subset
	GdtInstallDescriptor(0, 0, GDT_RING3_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, 0, GDT_RING3_DATA, GDT_GRANULARITY);

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

void
TssInitialize(
    _In_ int PrimaryCore)
{
	// Variables
	uint64_t tBase  = 0;
	uint32_t tLimit = 0;
    UUId_t CoreId   = ArchGetProcessorCoreId();

	// If we use the static allocator, it must be the boot cpu
	if (PrimaryCore) {
		TssPointers[CoreId] = &BootTss;
	}
	else {
		assert(CoreId < GDT_MAX_TSS);
		TssPointers[CoreId] = (TssDescriptor_t*)kmalloc(sizeof(TssDescriptor_t));
	}

	// Initialize descriptor by zeroing and set default members
	memset(TssPointers[CoreId], 0, sizeof(TssDescriptor_t));
	tBase   = (uint64_t)TssPointers[CoreId];
	tLimit  = tBase + sizeof(TssDescriptor_t);
    TssPointers[CoreId]->IoMapBase = (uint16_t)offsetof(TssDescriptor_t, IoMap[0]);

	// Install TSS into table and hardware
	TssInstall(GdtInstallDescriptor(tBase, tLimit, GDT_TSS_ENTRY, 0x00));
    if (PrimaryCore == 0) {
        TssCreateStacks();
    }
}

void
TssUpdateThreadStack(
    _In_ UUId_t     Cpu, 
    _In_ uintptr_t  Stack)
{
    assert(TssPointers[Cpu] != NULL);
	TssPointers[Cpu]->StackTable[0] = Stack;
}

/* TssCreateStacks 
 * Create safe stacks for #NMI, #DF, #DBG, #SS and #MCE. These are then
 * used for certain interrupts to support nesting by providing safe-stacks. */
void
TssCreateStacks(void)
{
    uint64_t Stacks = (uint64_t)kmalloc(PAGE_SIZE * 7);
    memset((void*)Stacks, 0, PAGE_SIZE * 7);
    Stacks += PAGE_SIZE * 7;

    for (int i = 0; i < 7; i++) {
        TssPointers[ArchGetProcessorCoreId()]->InterruptTable[i] = Stacks;
        Stacks -= PAGE_SIZE;
    }
}

uintptr_t
TssGetBootIoSpace(void)
{
	return (uintptr_t)&TssPointers[ArchGetProcessorCoreId()]->IoMap[0];
}

void
TssUpdateIo(
    _In_ UUId_t   Cpu,
    _In_ uint8_t* IoMap)
{
    assert(TssPointers[Cpu] != NULL);
	memcpy(&TssPointers[Cpu]->IoMap[0], IoMap, GDT_IOMAP_SIZE);
}

void
TssEnableIo(
    _In_ UUId_t   Cpu,
    _In_ uint16_t Port)
{
	size_t  Block  = Port / 8;
	size_t  Offset = Port % 8;
	uint8_t Bit    = (1u << Offset);
	
    assert(TssPointers[Cpu] != NULL);
	TssPointers[Cpu]->IoMap[Block] &= ~(Bit);
}

void
TssDisableIo(
    _In_ UUId_t   Cpu,
    _In_ uint16_t Port)
{
	size_t  Block  = Port / 8;
	size_t  Offset = Port % 8;
	uint8_t Bit    = (1u << Offset);
	
    assert(TssPointers[Cpu] != NULL);
	TssPointers[Cpu]->IoMap[Block] |= (Bit);
}
