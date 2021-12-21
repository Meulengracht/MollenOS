/**
 * Copyright 2021, Philip Meulengracht
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
 * Memory Initialization Functions
 *   - Implements helpers and utility functions with the MemoryInitialize.
 */

#define __TRACE

#include <assert.h>
#include <arch/mmu.h>
#include <ddk/io.h>
#include <debug.h>
#include <heap.h>
#include <machine.h>

static int                      g_bootMemoryEnabled     = 0;
static uintptr_t                g_bootMemoryStart       = 0;
static PlatformMemoryMapping_t* g_kernelMappings        = NULL;
static int                      g_kernelMappingIndex    = 0;
static int                      g_kernelMappingCapacity = 0;

// must not be static, should make a get method for this tho instead
uintptr_t g_bootMemoryAddress = 0;

// static methods in this file
static OsStatus_t __AllocateIdentity(size_t, void**);

static void
__PrintMemoryUsage(void) {
    int    maxBlocks       = READ_VOLATILE(GetMachine()->NumberOfMemoryBlocks);
    int    freeBlocks      = READ_VOLATILE(GetMachine()->NumberOfFreeMemoryBlocks);
    int    allocatedBlocks = maxBlocks - freeBlocks;
    size_t memoryInUse     = ((size_t)allocatedBlocks * (size_t)GetMemorySpacePageSize());

    TRACE("Memory in use %" PRIuIN " Bytes", memoryInUse);
    TRACE("Block status %" PRIuIN "/%" PRIuIN, allocatedBlocks, maxBlocks);
}

static size_t
__GetAvailablePhysicalMemory(
        _In_ struct VBoot* bootInformation)
{
    struct VBootMemoryEntry* entries;
    size_t                   memorySize = 0;
    TRACE("__GetAvailablePhysicalMemory(entries=0x%" PRIxIN ")", bootInformation->Memory.NumberOfEntries);

    entries = (struct VBootMemoryEntry*)bootInformation->Memory.Entries;
    for (unsigned int i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        TRACE("__GetAvailablePhysicalMemory found area PhysicalBase=0x%llx, Length=0x%llx",
              entries[i].PhysicalBase, entries[i].Length);
        if (entries[i].Type == VBootMemoryType_Available) {
            memorySize += entries[i].Length;
        }
    }

    TRACE("__GetAvailablePhysicalMemory available memory=0x%" PRIxIN, memorySize);
    return memorySize;
}

static OsStatus_t
__AddKernelMapping(
        _In_ paddr_t physicalBase,
        _In_ vaddr_t virtualBase,
        _In_ size_t  length)
{
    PlatformMemoryMapping_t* mapping;
    TRACE("__AddKernelMapping(physical=0x%" PRIxIN ", virtual=0x%" PRIxIN ", length=0x%" PRIxIN ")",
          physicalBase, virtualBase, length);

    if (g_kernelMappingIndex == g_kernelMappingCapacity - 1) {
        ERROR("__AddKernelMapping maximum number of kernel mappings reached");
        return OsOutOfMemory;
    }

    mapping = &g_kernelMappings[g_kernelMappingIndex++];
    mapping->PhysicalBase = physicalBase;
    mapping->VirtualBase  = virtualBase;
    mapping->Length       = length;
    return OsSuccess;
}

static OsStatus_t
__InitializeKernelMappings(
        _In_ PlatformMemoryConfiguration_t* memoryConfiguration)
{
    OsStatus_t osStatus;
    TRACE("__InitializeKernelMappings()");

    osStatus = __AllocateIdentity(memoryConfiguration->PageSize, (void**)&g_kernelMappings);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    memset(g_kernelMappings, 0, memoryConfiguration->PageSize);

    // calculate the max number of kernel mappings we support
    g_kernelMappingCapacity = (int)(memoryConfiguration->PageSize / sizeof(PlatformMemoryMapping_t));
    return OsSuccess;
}

static OsStatus_t
__InitializeIdentityMemory(
        _In_ struct VBoot*                  bootInformation,
        _In_ PlatformMemoryConfiguration_t* memoryConfiguration)
{
    struct VBootMemoryEntry* entries;
    size_t                   sizeRequired;
    TRACE("__InitializeIdentityMemory()");

    // We initially use a page for each memory mask manager. We also use a page for
    // kernel mappings, and we use a lot of pages for the GA region.
    sizeRequired = memoryConfiguration->MemoryMaskCount * memoryConfiguration->PageSize;
    sizeRequired += memoryConfiguration->PageSize;
    sizeRequired += StaticMemoryPoolCalculateSize(
            memoryConfiguration->MemoryMap.Shared.Length,
            memoryConfiguration->PageSize);

    // so this a very qualified guess and hopefully the preperation of the kernel virtual address space
    // should NEVER require more space than a megabyte of physical memory.
    TRACE("__InitializeIdentityMemory sizeRequired=0x%" PRIxIN, sizeRequired);
    entries = (struct VBootMemoryEntry*)bootInformation->Memory.Entries;
    for (unsigned int i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* entry = &entries[i];
        if (entry->Type == VBootMemoryType_Available) {
            if (entry->Length >= sizeRequired) {
                TRACE("__InitializeIdentityMemory found area PhysicalBase=0x%llx, Length=0x%llx",
                      entry->PhysicalBase, entry->Length);
                g_bootMemoryStart   = entry->PhysicalBase;
                g_bootMemoryAddress = entry->PhysicalBase;
                g_bootMemoryEnabled = 1;
                return OsSuccess;
            }
        }
    }
    return OsOutOfMemory;
}

static void
__DisableIdentityMemory(void)
{
    TRACE("__DisableIdentityMemory()");
    g_bootMemoryEnabled = 0;

    // Add the kernel mappings that have been created with identity mappings
    // 1. Physical Memory Managers
    // 2. GA Memory range (map it into itself)

}

OsStatus_t
__AllocateIdentity(
        _In_  size_t size,
        _Out_ void** memory)
{
    TRACE("__AllocateIdentity(size=0x%" PRIxIN ")", size);
    if (!g_bootMemoryEnabled) {
        return OsNotSupported;
    }

    // all allocations will be page aligned for good reasons
    // especially since allocations for page tables and whatnot are
    // made through here.
    if (size % GetMachine()->MemoryGranularity) {
        size += GetMachine()->MemoryGranularity;
        size -= (size % GetMachine()->MemoryGranularity);
    }

    *memory = (void*)g_bootMemoryAddress;
    g_bootMemoryAddress += size;
    return OsSuccess;
}

OsStatus_t
MachineAllocateBootMemory(
        _In_  size_t   size,
        _Out_ vaddr_t* virtualBaseOut,
        _Out_ paddr_t* physicalBaseOut)
{
    int        i          = GetMachine()->PhysicalMemory.MaskCount - 1;
    int        blockCount = DIVUP(size, GetMachine()->MemoryGranularity);
    OsStatus_t osStatus;
    uintptr_t  blocks[blockCount];
    vaddr_t    virtualBase;

    TRACE("MachineAllocateBootMemory(size=0x%" PRIxIN ")", size);

    // Allocate virtual space
    virtualBase = StaticMemoryPoolAllocate(&GetMachine()->GlobalAccessMemory, size);
    if (!virtualBase) {
        return OsOutOfMemory;
    }

    // Allocate a physical page
    for (; i >= 0 && blockCount; i--) {
        int pagesAllocated = blockCount;
        osStatus = MemoryStackPop(
                &GetMachine()->PhysicalMemory.Region[i].Stack,
                &pagesAllocated, &blocks[0]
        );
        if (osStatus == OsOutOfMemory) {
            TRACE("MachineAllocateBootMemory failed to allocate blocks %u from mask 0x%" PRIxIN,
                    osStatus, GetMachine()->PhysicalMemory.Masks[i]);
            continue;
        }
        blockCount -= pagesAllocated;
    }

    if (osStatus == OsSuccess) {
        TRACE("MachineAllocateBootMemory reverting blocks");
        // revert the order of pages, the reason for this is we want to provide
        // contigious pages, and it actually pops from top
        for (i = 0; i < blockCount / 2; i++) {
            uintptr_t temp = blocks[i];
            blocks[i] = blocks[blockCount - i];
            blocks[blockCount - i] = temp;
        }
    }

    // register the kernel mapping, so it will be available after switching to virtual space
    __AddKernelMapping(blocks[0], virtualBase, size);

    *physicalBaseOut = blocks[0];
    *virtualBaseOut  = virtualBase;
    return osStatus;
}

static OsStatus_t
__InitializePhysicalMemory(
        _In_ SystemMemoryAllocator_t*       physicalMemory,
        _In_ PlatformMemoryConfiguration_t* memoryConfiguration)
{
    OsStatus_t osStatus;
    int        i;
    TRACE("__InitializePhysicalMemory()");

    physicalMemory->MaskCount = memoryConfiguration->MemoryMaskCount;
    for (i = 0; i < memoryConfiguration->MemoryMaskCount; i++) {
        physicalMemory->Masks[i] = memoryConfiguration->MemoryMasks[i];
    }

    for (i = 0; i < memoryConfiguration->MemoryMaskCount; i++) {
        uintptr_t memory;
        osStatus = __AllocateIdentity(memoryConfiguration->PageSize, (void**)&memory);
        if (osStatus != OsSuccess) {
            return osStatus;
        }

        IrqSpinlockConstruct(&physicalMemory->Region[i].Lock);
        MemoryStackConstruct(&physicalMemory->Region[i].Stack,
                             memoryConfiguration->PageSize,
                             memory,
                             memoryConfiguration->PageSize
        );
    }
    return OsSuccess;
}

static void
__FillPhysicalMemory(
        _In_ struct VBoot*            bootInformation,
        _In_ SystemMemoryAllocator_t* physicalMemory,
        _In_ size_t                   pageSize)
{
    unsigned int             i;
    int                      j;
    size_t                   reservedMemorySize;
    struct VBootMemoryEntry* entries;
    TRACE("__FillPhysicalMemory()");

    // Calculate the used boot memory
    reservedMemorySize = g_bootMemoryAddress - g_bootMemoryStart;

    entries = (struct VBootMemoryEntry*)bootInformation->Memory.Entries;
    for (i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* entry = &entries[i];
        if (entry->Type == VBootMemoryType_Available) {
            uintptr_t baseAddress = (uintptr_t)entry->PhysicalBase;
            size_t    length      = (size_t)entry->Length;
            uintptr_t limit;

            // adjust values if we allocated this segment for boot memory
            if (baseAddress == g_bootMemoryStart) {
                baseAddress += reservedMemorySize;
                length      -= reservedMemorySize;
            }

            limit = baseAddress + length;
            TRACE("__FillPhysicalMemory region %i: 0x%" PRIxIN " => 0x%" PRIxIN, i, baseAddress, limit);
            for (j = 0; j < physicalMemory->MaskCount && length; j++) {
                size_t maskSize;
                size_t sizeAvailable;
                int    blockCount;

                if (baseAddress > physicalMemory->Masks[j]) {
                    continue;
                }

                // ok so base address fits into the region, lets calculate how many
                // blocks fit             mask=0xFFFFF, base=0, len=0x200000
                // maskSize = 0x100000
                maskSize      = (physicalMemory->Masks[j] - baseAddress) + 1;
                sizeAvailable = MIN(maskSize, length);
                blockCount    = (int)(sizeAvailable / pageSize);
                MemoryStackPush(&physicalMemory->Region[j].Stack, baseAddress, blockCount);

                // add statistics so we can keep track of free memory
                GetMachine()->NumberOfMemoryBlocks += (size_t)blockCount;
                GetMachine()->NumberOfFreeMemoryBlocks += (size_t)blockCount;

                // adjust base and length
                baseAddress += sizeAvailable;
                length      -= sizeAvailable;
            }
        }
    }
}

static OsStatus_t
__InitializeGlobalAccessMemory(
        _In_ StaticMemoryPool_t* globalAccessMemory,
        _In_ SystemMemoryMap_t*  memoryMap,
        _In_ size_t              pageSize)
{
    size_t     gaMemorySize;
    void*      gaMemory;
    OsStatus_t osStatus;
    TRACE("__InitializeGlobalAccessMemory()");

    gaMemorySize = StaticMemoryPoolCalculateSize(
            memoryMap->Shared.Length,
            pageSize
    );

    osStatus = __AllocateIdentity(gaMemorySize, &gaMemory);
    if (osStatus != OsSuccess) {
        return OsOutOfMemory;
    }

    TRACE("__InitializeGlobalAccessMemory initial size of ga memory to 0x%" PRIxIN, memoryMap->Shared.Length);
    gaMemorySize = memoryMap->Shared.Length;
    if (!IsPowerOfTwo(gaMemorySize)) {
        gaMemorySize = NextPowerOfTwo(gaMemorySize) >> 1U;
        TRACE("__InitializeGlobalAccessMemory adjusting size of ga memory to 0x%" PRIxIN, gaMemorySize);
    }

    StaticMemoryPoolConstruct(
            globalAccessMemory,
            gaMemory,
            memoryMap->Shared.Start,
            gaMemorySize,
            pageSize
    );
    return OsSuccess;
}

OsStatus_t
MachineInitializeMemorySystems(
        _In_ SystemMachine_t* machine)
{
    PlatformMemoryConfiguration_t configuration;
    size_t                        memorySize;
    OsStatus_t                    osStatus;
    TRACE("MachineInitializeMemorySystems()");

    if (!machine) {
        return OsInvalidParameters;
    }

    // Verify the size of available memory - require atleast 64mb (configurable some day)
    memorySize = __GetAvailablePhysicalMemory(&machine->BootInformation);
    if (memorySize < (64 * BYTES_PER_MB)) {
        ERROR("MachineInitializeMemorySystems available system memory was below 64mb (memorySize=%" PRIuIN "B)", memorySize);
        return OsInvalidParameters;
    }

    // Get fixed virtual memory layout and platform page size
    MmuGetMemoryConfiguration(&configuration);

    // Some of the configuration values we store in the machine structure for later use
    memcpy(&machine->MemoryMap, &configuration.MemoryMap, sizeof(SystemMemoryMap_t));
    machine->MemoryGranularity    = configuration.PageSize;
    machine->NumberOfMemoryBlocks = memorySize / machine->MemoryGranularity;

    // Find a suitable area where we can allocate continous physical pages for systems
    // that require (ident-mapped) memory. This is memory where the pointers need to be valid
    // both before and after we switch to the new virtual address space.
    osStatus = __InitializeIdentityMemory(&machine->BootInformation, &configuration);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    // Initialize the kernel mappings first. Kernel mappings are mappings we want the platform the do when
    // switching to the new virtual addressing space. This is to avoid having identity mapped memory when we are
    // done allocating for basic systems.
    osStatus = __InitializeKernelMappings(&configuration);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    // initialize the subsystems
    osStatus = __InitializePhysicalMemory(&machine->PhysicalMemory, &configuration);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    // Create the Global Access Memory Region which will be used to allocate kernel/system
    // virtual mappings that can be used to facilitate system services
    // It will start from the next free lower physical region and go up to
    // memoryMap->KernelRegion.Length
    __InitializeGlobalAccessMemory(
            &machine->GlobalAccessMemory,
            &machine->MemoryMap,
            machine->MemoryGranularity
    );

    // At this point no further boot allocations will be made, and thus we can start adding
    // the remaining free physical pages to the physical memory manager
    __DisableIdentityMemory();

    // fill the memory allocators, as we need them for allocating boot memory
    __FillPhysicalMemory(
            &machine->BootInformation,
            &machine->PhysicalMemory,
            machine->MemoryGranularity
    );

    // Switch to the new virtual space
#ifdef __OSCONFIG_HAS_MMIO
    osStatus = InitializeMemorySpace(
            &machine->SystemSpace,
            &machine->BootInformation,
            g_kernelMappings,
            g_kernelMappingIndex + 1
    );
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    // Invalidate memory map as it is no longer accessible
    machine->BootInformation.Memory.NumberOfEntries = 0;
    machine->BootInformation.Memory.Entries         = 0;
#endif

    // Initialize the slab allocator now that subsystems are up
    MemoryCacheInitialize();

    __PrintMemoryUsage();
    return OsSuccess;
}
