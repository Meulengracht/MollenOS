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

static int       g_bootMemoryEnabled = 0;
static uintptr_t g_bootMemoryStart   = 0;

// must not be static, should make a get method for this tho instead
uintptr_t g_bootMemoryAddress = 0;

static void
__PrintMemoryUsage(void) {
    int    maxBlocks       = READ_VOLATILE(GetMachine()->PhysicalMemory.capacity);
    int    freeBlocks      = READ_VOLATILE(GetMachine()->PhysicalMemory.index);
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
    TRACE("__GetAvailablePhysicalMemory(entries=0x%llx)", bootInformation->Memory.NumberOfEntries);

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
__InitializeBootMemory(
        _In_ struct VBoot*       bootInformation,
        _In_ size_t              memorySize,
        _In_ size_t              pageSize,
        _In_ SystemMemoryMap_t*  memoryMap)
{
    struct VBootMemoryEntry* entries;
    size_t                   sizeRequired;
    TRACE("__InitializeBootMemory()");

    // calculate a rough estimate of memory required for the stack and GA system
    // the stack uses 8mb per 4gb memory in 64 bit mode
    sizeRequired = DIVUP(memorySize, pageSize) * sizeof(void*);
    sizeRequired += StaticMemoryPoolCalculateSize(memoryMap->KernelRegion.Length, pageSize);

    // so this a very qualified guess and hopefully the preperation of the kernel virtual address space
    // should NEVER require more space than a megabyte of physical memory.
    sizeRequired += BYTES_PER_MB;
    TRACE("__InitializeBootMemory sizeRequired=0x%" PRIxIN, sizeRequired);
    entries = (struct VBootMemoryEntry*)bootInformation->Memory.Entries;
    for (unsigned int i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* entry = &entries[i];
        if (entry->Type == VBootMemoryType_Available) {
            // region also needs to be a part of the kernel map
            if (ISINRANGE(entry->PhysicalBase, memoryMap->KernelRegion.Start,
                          memoryMap->KernelRegion.Start + memoryMap->KernelRegion.Length)) {
                if (entry->Length >= sizeRequired) {
                    TRACE("__InitializeBootMemory found area PhysicalBase=0x%llx, Length=0x%llx",
                          entry->PhysicalBase, entry->Length);
                    g_bootMemoryStart   = entry->PhysicalBase;
                    g_bootMemoryAddress = entry->PhysicalBase;
                    g_bootMemoryEnabled = 1;
                    return OsSuccess;
                }
            }
        }
    }
    return OsOutOfMemory;
}

static void
__DisableBootMemory(void)
{
    TRACE("__DisableBootMemory()");
    g_bootMemoryEnabled = 0;
}

OsStatus_t
MachineAllocateBootMemory(
        _In_  size_t size,
        _Out_ void** memory)
{
    TRACE("MachineAllocateBootMemory(size=0x%" PRIxIN ")", size);
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

static OsStatus_t
__InitializePhysicalMemory(
        _In_  bounded_stack_t* boundedStack,
        _In_  size_t           pageSize,
        _In_  size_t           memorySize)
{
    size_t       memoryBlocks = (memorySize / pageSize);
    void*        memory;
    OsStatus_t   osStatus;
    TRACE("__InitializePhysicalMemory()");

    // initialize the stack, and then we pump memory onto it
    osStatus = MachineAllocateBootMemory(memoryBlocks * sizeof(void*), &memory);
    if (osStatus != OsSuccess) {
        return OsOutOfMemory;
    }
    bounded_stack_construct(boundedStack, (void**)memory, (int)memoryBlocks);
    return OsSuccess;
}

static void
__FillPhysicalMemory(
        _In_ struct VBoot*    bootInformation,
        _In_ bounded_stack_t* boundedStack,
        _In_ size_t           pageSize)
{
    unsigned int             i;
    size_t                   reservedMemorySize;
    struct VBootMemoryEntry* entries;
    TRACE("__FillPhysicalMemory()");

    // Calculate the used boot memory
    reservedMemorySize = g_bootMemoryAddress - g_bootMemoryStart;
    if (reservedMemorySize % pageSize) {
        reservedMemorySize += pageSize;
        reservedMemorySize -= (reservedMemorySize % pageSize);
    }

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
            while (baseAddress < limit) {
                bounded_stack_push(boundedStack, (void*)baseAddress);
                baseAddress += pageSize;
            }
        }
    }
}

static void
__InitializeGlobalAccessMemory(
        _In_ StaticMemoryPool_t* globalAccessMemory,
        _In_ SystemMemoryMap_t*  memoryMap,
        _In_ void*               gaMemory,
        _In_ size_t              pageSize)
{
    size_t gaMemorySize;
    size_t gaMemoryStart;
    TRACE("__InitializeGlobalAccessMemory()");

    gaMemoryStart = g_bootMemoryAddress;
    if (gaMemoryStart % pageSize) {
        gaMemoryStart += pageSize;
        gaMemoryStart -= (gaMemoryStart % pageSize);
    }

    gaMemorySize = memoryMap->KernelRegion.Length - gaMemoryStart;
    TRACE("__InitializeGlobalAccessMemory initial size of ga memory to 0x%" PRIxIN, gaMemorySize);
    if (!IsPowerOfTwo(gaMemorySize)) {
        gaMemorySize = NextPowerOfTwo(gaMemorySize) >> 1U;
        TRACE("__InitializeGlobalAccessMemory adjusting size of ga memory to 0x%" PRIxIN, gaMemorySize);
    }

    StaticMemoryPoolConstruct(
            globalAccessMemory,
            gaMemory,
            gaMemoryStart,
            gaMemorySize,
            pageSize
    );
}

OsStatus_t
MachineInitializeMemorySystems(
        _In_ SystemMachine_t* machine)
{
    size_t     memorySize;
    OsStatus_t osStatus;
    void*      gaMemory;
    size_t     gaMemorySize;
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
    MmuGetMemoryMapInformation(&machine->MemoryMap, &machine->MemoryGranularity);
    machine->NumberOfMemoryBlocks = memorySize / machine->MemoryGranularity;

    // Find a suitable area where we can allocate continous physical pages for systems
    // that require (ident-mapped) memory. This is memory where the pointers need to be valid
    // both before and after we switch to the new virtual address space.
    osStatus = __InitializeBootMemory(&machine->BootInformation,
                                      memorySize, machine->MemoryGranularity,
                                      &machine->MemoryMap);
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    // initialize the subsystems
    osStatus = __InitializePhysicalMemory(
            &machine->PhysicalMemory,
            machine->MemoryGranularity,
            memorySize
    );
    if (osStatus != OsSuccess) {
        return osStatus;
    }

    // Create the Global Access Memory Region which will be used to allocate kernel/system
    // virtual mappings that can be used to facilitate system services
    // It will start from the next free lower physical region and go up to
    // memoryMap->KernelRegion.Length
    gaMemorySize = StaticMemoryPoolCalculateSize(
            machine->MemoryMap.KernelRegion.Length - g_bootMemoryAddress,
            machine->MemoryGranularity
    );
    osStatus = MachineAllocateBootMemory(gaMemorySize, &gaMemory);
    if (osStatus != OsSuccess) {
        return OsOutOfMemory;
    }

    // Create the system kernel virtual memory space, this call identity maps all
    // memory allocated by AllocateBootMemory, and also allocates some itself
#ifdef __OSCONFIG_HAS_MMIO
    MmuPrepareKernel();
#endif

    // At this point no further boot allocations will be made, and thus we can start adding
    // the remaining free physical pages to the physical memory manager
    __DisableBootMemory();
    __FillPhysicalMemory(
            &machine->BootInformation,
            &machine->PhysicalMemory,
            machine->MemoryGranularity
    );

    // After the AllocateBootMemory+CreateKernelVirtualMemorySpace call, the reserved address
    // has moved again, which means we actually have allocated too much memory right
    // out the box, however we accept this memory waste, as it's max a few 10's of kB.
    __InitializeGlobalAccessMemory(
            &machine->GlobalAccessMemory,
            &machine->MemoryMap,
            gaMemory,
            machine->MemoryGranularity
    );

    // Switch to the new virtual space
#ifdef __OSCONFIG_HAS_MMIO
    osStatus = InitializeMemorySpace(&machine->SystemSpace, &machine->BootInformation);
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
