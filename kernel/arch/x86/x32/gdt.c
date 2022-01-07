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

extern void TssInstall(int gdtIndex);

GdtObject_t __GdtTableObject; // Don't make static, used in asm
static GdtDescriptor_t  g_descriptors[GDT_MAX_DESCRIPTORS] = { { 0 } };
static TssDescriptor_t* g_tssTable[GDT_MAX_TSS]            = { 0 };
static TssDescriptor_t  g_bootTss                          = { 0 };
static _Atomic(int)     g_gdtIndex                         = ATOMIC_VAR_INIT(0);

static int
GdtInstallDescriptor(
    _In_ uint32_t segmentBase,
    _In_ uint32_t segmentSize,
    _In_ uint8_t  permissions,
    _In_ uint8_t  granularity)
{
    int i = atomic_fetch_add(&g_gdtIndex, 1);
    assert(i < GDT_MAX_DESCRIPTORS);

	g_descriptors[i].BaseLow  = (uint16_t)(segmentBase & 0xFFFF);
    g_descriptors[i].BaseMid  = (uint8_t)((segmentBase >> 16) & 0xFF);
    g_descriptors[i].BaseHigh = (uint8_t)((segmentBase >> 24) & 0xFF);
    g_descriptors[i].LimitLow = (uint16_t)(segmentSize & 0xFFFF);
    g_descriptors[i].Flags    = (uint8_t)((segmentSize >> 16) & 0x0F);
    g_descriptors[i].Flags    |= (granularity & 0xF0);
    g_descriptors[i].Access   = permissions;
    return i;
}

void
GdtInitialize(void)
{
	// Setup gdt-table object
	__GdtTableObject.Limit  = (sizeof(GdtDescriptor_t) * GDT_MAX_DESCRIPTORS) - 1;
	__GdtTableObject.Base   = (uint32_t)&g_descriptors[0];

	// Install NULL descriptor
	GdtInstallDescriptor(0, 0, 0, 0);

	// Kernel segments
	// Kernel segments span the entire virtual address space from 0 -> 0xFFFFFFFF
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING0_LIMIT / PAGE_SIZE,
                         GDT_RING0_CODE, GDT_GRANULARITY_4KB);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING0_LIMIT,
                         GDT_RING0_DATA, GDT_GRANULARITY_4KB);
    GdtInstallDescriptor(
            MEMORY_LOCATION_TLS_START,
            (MEMORY_SEGMENT_TLS_SIZE - 1) / PAGE_SIZE,
            GDT_RING0_DATA, GDT_GRANULARITY_4KB
    );

	// Application segments
    // Applications are not allowed full access of addressing space
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING3_LIMIT / PAGE_SIZE,
                         GDT_RING3_CODE, GDT_GRANULARITY_4KB);
	GdtInstallDescriptor(0, MEMORY_SEGMENT_RING3_LIMIT,
                         GDT_RING3_DATA, GDT_GRANULARITY_4KB);
	GdtInstallDescriptor(
            MEMORY_SEGMENT_TLS_BASE,
            (MEMORY_SEGMENT_TLS_SIZE - 1) / PAGE_SIZE,
            GDT_RING3_DATA, GDT_GRANULARITY_4KB
    );

	// Prepare gdt and tss for boot cpu
	GdtInstall();
	TssInitialize(1);
}

void
TssInitialize(
        _In_ int bsp)
{
	uint32_t tssBase;
	uint32_t tssLimit;
    UUId_t   coreId = ArchGetProcessorCoreId();

	// If we use the static allocator, it must be the boot cpu
	if (bsp) {
        g_tssTable[coreId] = &g_bootTss;
	}
	else {
		assert(coreId < GDT_MAX_TSS);
        g_tssTable[coreId] = (TssDescriptor_t*)kmalloc(sizeof(TssDescriptor_t));
	}

	// Initialize descriptor by zeroing and set default members
	memset(g_tssTable[coreId], 0, sizeof(TssDescriptor_t));
    tssBase  = (uint32_t)g_tssTable[coreId];
    tssLimit = tssBase + sizeof(TssDescriptor_t);

	// Setup TSS initial ring0 stack information
	// this will be filled out properly later by scheduler
	g_tssTable[coreId]->Ss0 = GDT_KDATA_SEGMENT;
    g_tssTable[coreId]->Ss2 = GDT_RING3_DATA + 0x03;
	
	// Set initial segment information (Ring0)
	g_tssTable[coreId]->Cs        = GDT_KCODE_SEGMENT;
    g_tssTable[coreId]->Ss        = GDT_KDATA_SEGMENT;
    g_tssTable[coreId]->Ds        = GDT_KDATA_SEGMENT;
    g_tssTable[coreId]->Es        = GDT_KDATA_SEGMENT;
    g_tssTable[coreId]->Fs        = GDT_KTLS_SEGMENT;
    g_tssTable[coreId]->Gs        = GDT_KTLS_SEGMENT;
    g_tssTable[coreId]->IoMapBase = (uint16_t)offsetof(TssDescriptor_t, IoMap[0]);

	// Install TSS into table and hardware
	TssInstall(
            GdtInstallDescriptor(
                    tssBase,
                    tssLimit,
                    GDT_TSS_ENTRY,
                    0x00
            )
    );
}

void
TssUpdateThreadStack(
    _In_ UUId_t    coreId,
    _In_ uintptr_t stackAddress)
{
    assert(g_tssTable[coreId] != NULL);
    g_tssTable[coreId]->Esp0 = stackAddress;
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
	size_t  Block  = Port / 8;
	size_t  Offset = Port % 8;
	uint8_t Bit    = (1u << Offset);
	
    assert(g_tssTable[Cpu] != NULL);
    g_tssTable[Cpu]->IoMap[Block] &= ~(Bit);
}

void
TssDisableIo(
    _In_ UUId_t   Cpu,
    _In_ uint16_t Port)
{
	size_t  Block  = Port / 8;
	size_t  Offset = Port % 8;
	uint8_t Bit    = (1u << Offset);
	
    assert(g_tssTable[Cpu] != NULL);
    g_tssTable[Cpu]->IoMap[Block] |= (Bit);
}
