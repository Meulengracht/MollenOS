/**
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
 * x86-32 Descriptor Table
 * - Global Descriptor Table
 * - Task State Segment 
 */

#include <arch/x86/arch.h>
#include <arch/x86/memory.h>
#include <arch/x86/x32/gdt.h>
#include <assert.h>
#include <component/cpu.h>
#include <heap.h>
#include <string.h>

extern void TssInstall(int gdtIndex);

GdtObject_t            __GdtTableObject; // Don't make static, used in asm
static GdtDescriptor_t g_descriptors[GDT_MAX_DESCRIPTORS] = { { 0 } };
static _Atomic(int)    g_gdtIndex                         = ATOMIC_VAR_INIT(0);

static int
__InstallDescriptor(
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
    __InstallDescriptor(0, 0, 0, 0);

	// Kernel segments
	// Kernel segments span the entire virtual address space from 0 -> 0xFFFFFFFF
    __InstallDescriptor(0, MEMORY_SEGMENT_RING0_LIMIT / PAGE_SIZE,
                        GDT_RING0_CODE, GDT_GRANULARITY_4KB);
    __InstallDescriptor(0, MEMORY_SEGMENT_RING0_LIMIT,
                        GDT_RING0_DATA, GDT_GRANULARITY_4KB);
    __InstallDescriptor(
            MEMORY_LOCATION_TLS_START,
            (MEMORY_SEGMENT_TLS_SIZE - 1) / PAGE_SIZE,
            GDT_RING0_DATA, GDT_GRANULARITY_4KB
    );

	// Application segments
    // Applications are not allowed full access of addressing space
    __InstallDescriptor(0, MEMORY_SEGMENT_RING3_LIMIT / PAGE_SIZE,
                        GDT_RING3_CODE, GDT_GRANULARITY_4KB);
    __InstallDescriptor(0, MEMORY_SEGMENT_RING3_LIMIT,
                        GDT_RING3_DATA, GDT_GRANULARITY_4KB);
    __InstallDescriptor(
            MEMORY_SEGMENT_TLS_BASE,
            (MEMORY_SEGMENT_TLS_SIZE - 1) / PAGE_SIZE,
            GDT_RING3_DATA, GDT_GRANULARITY_4KB
    );
	GdtInstall();
}

OsStatus_t
TssInitialize(
        _In_ PlatformCpuCoreBlock_t* coreBlock)
{
    MemorySpace_t*   memorySpace;
    TssDescriptor_t* tssDescriptor;
	uint32_t         tssBase;
	uint32_t         tssLimit;

    assert(coreBlock != NULL);

    coreBlock->Tss = kmalloc(sizeof(TssDescriptor_t));
    if (!coreBlock->Tss) {
        return OsOutOfMemory;
    }

    tssDescriptor = coreBlock->Tss;

	// Initialize descriptor by zeroing and set default members
	memset(tssDescriptor, 0, sizeof(TssDescriptor_t));
    tssBase  = (uint32_t)tssDescriptor;
    tssLimit = tssBase + sizeof(TssDescriptor_t);

	// Setup TSS initial ring0 stack information
	// this will be filled out properly later by scheduler
	tssDescriptor->Ss0 = GDT_KDATA_SEGMENT;
    tssDescriptor->Ss2 = GDT_RING3_DATA + 0x03;
	
	// Set initial segment information (Ring0)
	tssDescriptor->Cs        = GDT_KCODE_SEGMENT;
    tssDescriptor->Ss        = GDT_KDATA_SEGMENT;
    tssDescriptor->Ds        = GDT_KDATA_SEGMENT;
    tssDescriptor->Es        = GDT_KDATA_SEGMENT;
    tssDescriptor->Fs        = GDT_KTLS_SEGMENT;
    tssDescriptor->Gs        = GDT_KTLS_SEGMENT;
    tssDescriptor->IoMapBase = (uint16_t)offsetof(TssDescriptor_t, IoMap[0]);

    // The core might be missing the io-map, so update it now
    memorySpace = GetCurrentMemorySpace();
    if (memorySpace && !memorySpace->PlatfromData.TssIoMap) {
        memorySpace->PlatfromData.TssIoMap = &tssDescriptor->IoMap[0];
    }

	// Install TSS into table and hardware
	TssInstall(
            __InstallDescriptor(
                    tssBase,
                    tssLimit,
                    GDT_TSS_ENTRY,
                    0x00
            )
    );
    return OsSuccess;
}

void
TssUpdateThreadStack(
        _In_ PlatformCpuCoreBlock_t* coreBlock,
        _In_ uintptr_t               stackAddress)
{
    TssDescriptor_t* tssDescriptor;
    assert(coreBlock != NULL);

    tssDescriptor = coreBlock->Tss;
    assert(tssDescriptor != NULL);

    tssDescriptor->Esp0 = stackAddress;
}

void
TssUpdateIo(
        _In_ PlatformCpuCoreBlock_t* coreBlock,
        _In_ uint8_t*                ioMap)
{
    TssDescriptor_t* tssDescriptor;
    assert(coreBlock != NULL);

    tssDescriptor = coreBlock->Tss;
    assert(tssDescriptor != NULL);

    memcpy(&tssDescriptor->IoMap[0], ioMap, GDT_IOMAP_SIZE);
}

void
TssEnableIo(
        _In_ PlatformCpuCoreBlock_t* coreBlock,
        _In_ uint16_t                port)
{
    TssDescriptor_t* tssDescriptor;
	size_t           block  = port / 8;
	size_t           offset = port % 8;
	uint8_t          bit    = (1u << offset);

    assert(coreBlock != NULL);

    tssDescriptor = coreBlock->Tss;
    assert(tssDescriptor != NULL);
    tssDescriptor->IoMap[block] &= ~(bit);
}

void
TssDisableIo(
        _In_ PlatformCpuCoreBlock_t* coreBlock,
        _In_ uint16_t                port)
{
    TssDescriptor_t* tssDescriptor;
    size_t           block  = port / 8;
    size_t           offset = port % 8;
    uint8_t          bit    = (1u << offset);

    assert(coreBlock != NULL);

    tssDescriptor = coreBlock->Tss;
    assert(tssDescriptor != NULL);
    tssDescriptor->IoMap[block] |= (bit);
}
