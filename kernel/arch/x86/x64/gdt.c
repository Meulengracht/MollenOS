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
 * x86-64 Descriptor Table
 * - Global Descriptor Table
 * - Task State Segment 
 */

#include <arch/utils.h>
#include <arch/x86/arch.h>
#include <arch/x86/memory.h>
#include <arch/x86/x64/gdt.h>
#include <assert.h>
#include <component/cpu.h>
#include <heap.h>
#include <string.h>

extern void TssInstall(int GdtIndex);

GdtObject_t            g_gdtTable; // Don't make static, used in asm
static GdtDescriptor_t g_descriptorTable[GDT_MAX_DESCRIPTORS] = { { 0 } };
static _Atomic(int)    g_nextGdtIndex                         = ATOMIC_VAR_INIT(0);

/**
 * @brief Create safe stacks for #NMI, #DF, #DB, #PF and #MCE. These are then
 * used for certain interrupts to support nesting by providing safe-stacks.
 */
OsStatus_t
__CreateTssStacks(
        _In_ TssDescriptor_t* tssDescriptor)
{
    uint64_t allStacks = (uint64_t)kmalloc(PAGE_SIZE * 7);
    if (!allStacks) {
        return OsOutOfMemory;
    }

    memset((void*)allStacks, 0, PAGE_SIZE * 7);
    allStacks += PAGE_SIZE * 7;

    for (int i = 0; i < 7; i++) {
        tssDescriptor->InterruptTable[i] = allStacks;
        allStacks -= PAGE_SIZE;
    }
    return OsSuccess;
}

static int
__InstallDescriptor(
    _In_ uint64_t baseAddress,
    _In_ uint32_t limit,
    _In_ uint8_t  accessFlags,
    _In_ uint8_t  upperFlags)
{
    int i = atomic_fetch_add(&g_nextGdtIndex, 1);
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
    __InstallDescriptor(0, 0, 0, 0);

	// Kernel segments
    __InstallDescriptor(0, 0, GDT_RING0_CODE, GDT_FLAG_64BIT);
    __InstallDescriptor(0, 0, GDT_RING0_DATA, GDT_FLAG_64BIT);

	// Applications segments
    __InstallDescriptor(0, 0, GDT_RING3_CODE, GDT_FLAG_64BIT);
    __InstallDescriptor(0, 0, GDT_RING3_DATA, GDT_FLAG_64BIT);

	// Shared segments
	// So the GS base segment should be initialized with the kernel core gs address
	// which is the one that will be loaded from swapgs
    __InstallDescriptor(0, 0, GDT_RING3_DATA, GDT_FLAG_64BIT | GDT_FLAG_PAGES);
	GdtInstall();
}

OsStatus_t
TssInitialize(
        _In_ PlatformCpuCoreBlock_t* coreBlock)
{
    MemorySpace_t*   memorySpace;
    TssDescriptor_t* tssDescriptor;
	uint64_t         tssBase;
	uint64_t         tssLimit;

    assert(coreBlock != NULL);

    coreBlock->Tss = kmalloc(sizeof(TssDescriptor_t));
    if (!coreBlock->Tss) {
        return OsOutOfMemory;
    }

    tssDescriptor = coreBlock->Tss;

	// Initialize descriptor by zeroing and set default members
	memset(tssDescriptor, 0, sizeof(TssDescriptor_t));
    tssBase  = (uint64_t)tssDescriptor;
    tssLimit = tssBase + sizeof(TssDescriptor_t);
    tssDescriptor->IoMapBase = (uint16_t)offsetof(TssDescriptor_t, IoMap[0]);
    if (__CreateTssStacks(tssDescriptor) != OsSuccess) {
        kfree(coreBlock->Tss);
        return OsOutOfMemory;
    }

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

    tssDescriptor->StackTable[0] = stackAddress;
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
