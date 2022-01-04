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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * x86-32 Descriptor Table
 * - Global Descriptor Table
 * - Task State Segment 
 */

#include <arch/x86/arch.h>
#include <arch/x86/memory.h>
#include <arch/x86/x32/gdt.h>
#include <arch/utils.h>
#include <assert.h>
#include <string.h>
#include <heap.h>

extern void TssInstall(int GdtIndex);

GdtObject_t __GdtTableObject; // Don't make static, used in asm
static GdtDescriptor_t Descriptors[GDT_MAX_DESCRIPTORS] = { { 0 } };
static TssDescriptor_t *TssPointers[GDT_MAX_TSS]        = { 0 };
static TssDescriptor_t BootTss                          = { 0 };
static _Atomic(int) GdtIndicer                          = ATOMIC_VAR_INIT(0);

static int
GdtInstallDescriptor(
    _In_ uint32_t Base, 
    _In_ uint32_t Limit,
    _In_ uint8_t  Access, 
    _In_ uint8_t  Grandularity)
{
    int GdtIndex = atomic_fetch_add(&GdtIndicer, 1);
	
	// Fill descriptor
	Descriptors[GdtIndex].BaseLow  = (uint16_t)(Base & 0xFFFF);
	Descriptors[GdtIndex].BaseMid  = (uint8_t)((Base >> 16) & 0xFF);
	Descriptors[GdtIndex].BaseHigh = (uint8_t)((Base >> 24) & 0xFF);
	Descriptors[GdtIndex].LimitLow = (uint16_t)(Limit & 0xFFFF);
	Descriptors[GdtIndex].Flags    = (uint8_t)((Limit >> 16) & 0x0F);
	Descriptors[GdtIndex].Flags    |= (Grandularity & 0xF0);
	Descriptors[GdtIndex].Access   = Access;
    return GdtIndex;
}

void
GdtInitialize(void)
{
	// Setup gdt-table object
	__GdtTableObject.Limit  = (sizeof(GdtDescriptor_t) * GDT_MAX_DESCRIPTORS) - 1;
	__GdtTableObject.Base   = (uint32_t)&Descriptors[0];

	// Install NULL descriptor
	GdtInstallDescriptor(0, 0, 0, 0);

	// Kernel segments
	// Kernel segments span the entire virtual address space from 0 -> 0xFFFFFFFF
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING0_LIMIT / PAGE_SIZE,
		GDT_RING0_CODE, GDT_GRANULARITY);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING0_LIMIT,
		GDT_RING0_DATA, GDT_GRANULARITY);

	// Application segments
    // Applications are not allowed full access of addressing space
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

void
TssInitialize(
    _In_ int PrimaryCore)
{
	uint32_t tBase  = 0;
	uint32_t tLimit = 0;
    UUId_t   CoreId = ArchGetProcessorCoreId();

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

void
TssUpdateThreadStack(
    _In_ UUId_t    Cpu, 
    _In_ uintptr_t Stack)
{
    assert(TssPointers[Cpu] != NULL);
	TssPointers[Cpu]->Esp0 = Stack;
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
