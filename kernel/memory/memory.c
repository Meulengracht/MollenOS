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

uintptr_t g_bootMemoryStart   = 0;
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
    size_t upperSize = 0;
    for (unsigned int i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        if (bootInformation->Memory.Entries[i].Type == VBootMemoryType_Available) {
            upperSize += bootInformation->Memory.Entries[i].Length;
        }
    }
    return upperSize;
}

static OsStatus_t
__InitializeBootMemory(
        _In_  struct VBoot*       bootInformation,
        _In_  SystemMemoryMap_t*  memoryMap)
{
    for (unsigned int i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* entry = &bootInformation->Memory.Entries[i];
        if (entry->Type == VBootMemoryType_Available) {
            // region also needs to be a part of the kernel map
            if (ISINRANGE(memoryMap->KernelRegion.Start, entry->PhysicalBase, entry->PhysicalBase + entry->Length)) {
                // region also needs to fit atleast 16mb
                if (entry->Length >= (16 * BYTES_PER_MB)) {
                    g_bootMemoryStart   = entry->PhysicalBase;
                    g_bootMemoryAddress = entry->PhysicalBase;
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
    g_bootMemoryAddress = 0;
    g_bootMemoryStart   = 0;
}

OsStatus_t
MachineAllocateBootMemory(
        _In_  size_t size,
        _Out_ void** memory)
{
    if (!g_bootMemoryAddress) {
        return OsNotSupported;
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
    unsigned int i;
    size_t       reservedMemorySize;

    // Calculate the used boot memory
    reservedMemorySize = g_bootMemoryAddress - g_bootMemoryStart;
    if (reservedMemorySize % pageSize) {
        reservedMemorySize += pageSize;
        reservedMemorySize -= (reservedMemorySize % pageSize);
    }

    for (i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* entry = &bootInformation->Memory.Entries[i];
        if (entry->Type == VBootMemoryType_Available) {
            uintptr_t baseAddress = (uintptr_t)entry->PhysicalBase;
            uintptr_t limit;
            if (baseAddress == g_bootMemoryStart) {
                baseAddress += reservedMemorySize;
            }

            limit = baseAddress + (uintptr_t)(entry->Length - reservedMemorySize);
            TRACE("[pmem] [mem_init] region %i: 0x%" PRIxIN " => 0x%" PRIxIN, i, baseAddress, limit);
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

    gaMemoryStart = g_bootMemoryAddress;
    if (gaMemoryStart % pageSize) {
        gaMemoryStart += pageSize;
        gaMemoryStart -= (gaMemoryStart % pageSize);
    }

    gaMemorySize = memoryMap->KernelRegion.Length - gaMemoryStart;
    TRACE("[pmem] [mem_init] initial size of ga memory to 0x%" PRIxIN, gaMemorySize);
    if (!IsPowerOfTwo(gaMemorySize)) {
        gaMemorySize = NextPowerOfTwo(gaMemorySize) >> 1U;
        TRACE("[pmem] [mem_init] adjusting size of ga memory to 0x%" PRIxIN, gaMemorySize);
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
    osStatus = __InitializeBootMemory(&machine->BootInformation, &machine->MemoryMap);
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
#endif

    // Initialize the slab allocator now that subsystems are up
    MemoryCacheInitialize();

    __PrintMemoryUsage();
    return OsSuccess;
}
