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

GdtObject_t             g_gdtTable; // Don't make static, used in asm
static GdtDescriptor_t  g_descriptorTable[GDT_MAX_DESCRIPTORS] = { { 0 } };
static TssDescriptor_t* g_tssTable[GDT_MAX_TSS]   = { 0 };
static TssDescriptor_t  g_bootTss                 = { 0 };
static _Atomic(int)     g_nextGdtIndex            = ATOMIC_VAR_INIT(0);

static int
GdtInstallDescriptor(
    _In_ uint64_t baseAddress,
    _In_ uint32_t limit,
    _In_ uint8_t  accessFlags,
    _In_ uint8_t  upperFlags)
{
    int i = atomic_fetch_add(&g_nextGdtIndex, 1);

	// Fill descriptor
	g_descriptorTable[i].BaseLow   = (uint16_t)(baseAddress & 0xFFFF);
    g_descriptorTable[i].BaseMid   = (uint8_t)((baseAddress >> 16) & 0xFF);
    g_descriptorTable[i].BaseHigh  = (uint8_t)((baseAddress >> 24) & 0xFF);
    g_descriptorTable[i].BaseUpper = (uint32_t)((baseAddress >> 32) & 0xFFFFFFFF);
    g_descriptorTable[i].LimitLow  = (uint16_t)(limit & 0xFFFF);
    g_descriptorTable[i].Flags     = (uint8_t)((limit >> 16) & 0x0F);
    g_descriptorTable[i].Flags     |= (upperFlags & 0xF0);
    g_descriptorTable[i].Access    = accessFlags;
    g_descriptorTable[i].Reserved  = 0;
    return i;
}

void
GdtInitialize(void)
{
	// Setup gdt-table object
	g_gdtTable.Limit = (sizeof(GdtDescriptor_t) * GDT_MAX_DESCRIPTORS) - 1;
    g_gdtTable.Base  = (uint64_t)&g_descriptorTable[0];

	// Install NULL descriptor
	GdtInstallDescriptor(0, 0, 0, 0);

	// Kernel segments
	GdtInstallDescriptor(0, 0, GDT_RING0_CODE, GDT_FLAG_64BIT);
	GdtInstallDescriptor(0, 0, GDT_RING0_DATA, GDT_FLAG_64BIT);

	// Applications segments
	GdtInstallDescriptor(0, 0, GDT_RING3_CODE, GDT_FLAG_64BIT);
	GdtInstallDescriptor(0, 0, GDT_RING3_DATA, GDT_FLAG_64BIT);

	// Shared segments
	// So the GS base segment should be initialized with the kernel core gs address
	// which is the one that will be loaded from swapgs
	GdtInstallDescriptor(0, (MEMORY_SEGMENT_EXTRA_SIZE - 1) / PAGE_SIZE,
		GDT_RING3_DATA, GDT_FLAG_64BIT | GDT_FLAG_PAGES);

	// Prepare gdt and tss for boot cpu
	GdtInstall();
	TssInitialize(1);
}

void
TssInitialize(
    _In_ int PrimaryCore)
{
    UUId_t   coreId = ArchGetProcessorCoreId();
	uint64_t tssBase;
	uint32_t tssLimit;

	// If we use the static allocator, it must be the boot cpu
	if (PrimaryCore) {
        g_tssTable[coreId] = &g_bootTss;
	}
	else {
		assert(coreId < GDT_MAX_TSS);
        g_tssTable[coreId] = (TssDescriptor_t*)kmalloc(sizeof(TssDescriptor_t));
	}

	// Initialize descriptor by zeroing and set default members
	memset(g_tssTable[coreId], 0, sizeof(TssDescriptor_t));
    tssBase  = (uint64_t)g_tssTable[coreId];
    tssLimit = tssBase + sizeof(TssDescriptor_t);
    g_tssTable[coreId]->IoMapBase = (uint16_t)offsetof(TssDescriptor_t, IoMap[0]);

	// Install TSS into table and hardware
	TssInstall(GdtInstallDescriptor(tssBase, tssLimit, GDT_TSS_ENTRY, 0x00));
    if (PrimaryCore == 0) {
        TssCreateStacks();
    }
}

void
TssUpdateThreadStack(
    _In_ UUId_t     Cpu, 
    _In_ uintptr_t  Stack)
{
    assert(g_tssTable[Cpu] != NULL);
    g_tssTable[Cpu]->StackTable[0] = Stack;
}

/* TssCreateStacks 
 * Create safe stacks for #NMI, #DF, #DB, #PF and #MCE. These are then
 * used for certain interrupts to support nesting by providing safe-stacks. */
void
TssCreateStacks(void)
{
    uint64_t allStacks = (uint64_t)kmalloc(PAGE_SIZE * 7);
    assert(allStacks != 0);

    memset((void*)allStacks, 0, PAGE_SIZE * 7);
    allStacks += PAGE_SIZE * 7;

    for (int i = 0; i < 7; i++) {
        g_tssTable[ArchGetProcessorCoreId()]->InterruptTable[i] = allStacks;
        allStacks -= PAGE_SIZE;
    }
}

uintptr_t
TssGetBootIoSpace(void)
{
	return (uintptr_t)&g_tssTable[ArchGetProcessorCoreId()]->IoMap[0];
}

void
TssUpdateIo(
    _In_ UUId_t   Cpu,
    _In_ uint8_t* IoMap)
{
    assert(g_tssTable[Cpu] != NULL);
	memcpy(&g_tssTable[Cpu]->IoMap[0], IoMap, GDT_IOMAP_SIZE);
}

void
TssEnableIo(
    _In_ UUId_t   Cpu,
    _In_ uint16_t Port)
{
	size_t  block  = Port / 8;
	size_t  offset = Port % 8;
	uint8_t bit    = (1u << offset);
	
    assert(g_tssTable[Cpu] != NULL);
    g_tssTable[Cpu]->IoMap[block] &= ~(bit);
}

void
TssDisableIo(
    _In_ UUId_t   Cpu,
    _In_ uint16_t Port)
{
	size_t  block  = Port / 8;
	size_t  offset = Port % 8;
	uint8_t bit    = (1u << offset);
	
    assert(g_tssTable[Cpu] != NULL);
    g_tssTable[Cpu]->IoMap[block] |= (bit);
}
