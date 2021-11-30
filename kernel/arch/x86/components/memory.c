/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * Memory Utility Functions
 *   - Implements helpers and utility functions with the MemoryInitialize.
 */
#define __MODULE "MEM0"
#define __TRACE

#include <arch.h>
#include <assert.h>
#include <arch/utils.h>
#include <cpu.h>
#include <ddk/io.h>
#include <debug.h>
#include <gdt.h>
#include <machine.h>
#include <memory.h>

// Interface to the arch-specific
extern PAGE_MASTER_LEVEL* MmVirtualGetMasterTable(MemorySpace_t* memorySpace, vaddr_t address,
                                                  PAGE_MASTER_LEVEL** parentDirectory, int* isCurrentOut);
extern PageTable_t* MmVirtualGetTable(PAGE_MASTER_LEVEL* parentPageDirectory, PAGE_MASTER_LEVEL* pageDirectory,
                                      vaddr_t address, int isCurrent, int createIfMissing, int* update);

extern void memory_invalidate_addr(uintptr_t pda);
extern void memory_load_cr3(uintptr_t pda);
extern void memory_reload_cr3(void);

uintptr_t g_lastReservedAddress = 0;

// Disable the atomic wrong alignment, as they are aligned and are sanitized
// in the arch-specific layer
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

void
PrintPhysicalMemoryUsage(void) {
    int    maxBlocks       = READ_VOLATILE(GetMachine()->PhysicalMemory.capacity);
    int    freeBlocks      = READ_VOLATILE(GetMachine()->PhysicalMemory.index);
    int    allocatedBlocks = maxBlocks - freeBlocks;
    size_t reservedMemory  = READ_VOLATILE(g_lastReservedAddress);
    size_t memoryInUse     = reservedMemory + ((size_t)allocatedBlocks * (size_t)PAGE_SIZE);
    
    TRACE("Memory in use %" PRIuIN " Bytes", memoryInUse);
    TRACE("Block status %" PRIuIN "/%" PRIuIN, allocatedBlocks, maxBlocks);
    TRACE("Reserved memory: 0x%" PRIxIN " (%" PRIuIN " blocks)", g_lastReservedAddress, g_lastReservedAddress / PAGE_SIZE);
}

uintptr_t
AllocateBootMemory(
    _In_ size_t size)
{
    uintptr_t memory = READ_VOLATILE(g_lastReservedAddress);
    uintptr_t nextMemory;
    if (!memory) {
        return 0;
    }

    nextMemory = memory + size;
    if (nextMemory % PAGE_SIZE) {
        nextMemory += PAGE_SIZE - (nextMemory % PAGE_SIZE);
    }
    WRITE_VOLATILE(g_lastReservedAddress, nextMemory);
    return memory;
}

size_t
__GetAvailablePhysicalMemory(
        _In_ struct VBoot* bootInformation)
{
    size_t upperSize = 0;
    for (unsigned int i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        uintptr_t limit = bootInformation->Memory.Entries[i].PhysicalBase +
                bootInformation->Memory.Entries[i].Length;
        if (bootInformation->Memory.Entries[i].Type == VBootMemoryType_Available) {
            if (limit > upperSize) {
                upperSize = limit;
            }
        }
    }
    return upperSize;
}

OsStatus_t
InitializeSystemMemory(
    _In_  struct VBoot*       bootInformation,
    _In_  bounded_stack_t*    boundedStack,
    _In_  StaticMemoryPool_t* globalAccessMemory,
    _In_  SystemMemoryMap_t*  memoryMap,
    _Out_ size_t*             memoryGranularityOut,
    _Out_ size_t*             numberOfMemoryBlocksOut)
{
    size_t     memorySize;
    uintptr_t  gaMemory;
    size_t     gaMemorySize;
    size_t     count;
    OsStatus_t osStatus;
    int        i;

    if (!bootInformation || !boundedStack || !globalAccessMemory ||
        !memoryMap || !memoryGranularityOut || !numberOfMemoryBlocksOut) {
        return OsInvalidParameters;
    }

    // The memory-high part is 64kb blocks 
    // whereas the memory-low part is bytes of memory
    memorySize = __GetAvailablePhysicalMemory(bootInformation);
    assert((memorySize / 1024 / 1024) >= 64);
    
    // Initialize the reserved memory address
    WRITE_VOLATILE(g_lastReservedAddress, MEMORY_LOCATION_RESERVED);
    
    *memoryGranularityOut    = PAGE_SIZE;
    *numberOfMemoryBlocksOut = DIVUP(memorySize, PAGE_SIZE);

    memoryMap->KernelRegion.Start  = 0;
    memoryMap->KernelRegion.Length = MEMORY_LOCATION_KERNEL_END;

    memoryMap->UserCode.Start  = MEMORY_LOCATION_RING3_CODE;
    memoryMap->UserCode.Length = MEMORY_LOCATION_RING3_CODE_END - MEMORY_LOCATION_RING3_CODE;

    memoryMap->UserHeap.Start  = MEMORY_LOCATION_RING3_HEAP;
    memoryMap->UserHeap.Length = MEMORY_LOCATION_RING3_HEAP_END - MEMORY_LOCATION_RING3_HEAP;

    memoryMap->ThreadRegion.Start  = MEMORY_LOCATION_RING3_THREAD_START;
    memoryMap->ThreadRegion.Length = MEMORY_LOCATION_RING3_THREAD_END - MEMORY_LOCATION_RING3_THREAD_START;
    
    // Create the physical memory map
    count = memorySize / PAGE_SIZE;
    bounded_stack_construct(boundedStack, (void*)AllocateBootMemory(count * sizeof(void*)), (int)count);
    
    // Create the global access memory, it needs to start after the last reserved
    // memory address, because the reserved memory is not freeable or allocatable.
    gaMemorySize = MEMORY_LOCATION_VIDEO - READ_VOLATILE(g_lastReservedAddress);
    gaMemory     = AllocateBootMemory(StaticMemoryPoolCalculateSize(gaMemorySize, PAGE_SIZE));
    
    // Create the system kernel virtual memory space, this call identity maps all
    // memory allocated by AllocateBootMemory, and also allocates some itself
    osStatus = CreateKernelVirtualMemorySpace();
    if (osStatus != OsSuccess) {
        return osStatus;
    }
    
    // After the AllocateBootMemory+CreateKernelVirtualMemorySpace call, the reserved address 
    // has moved again, which means we actually have allocated too much memory right 
    // out the box, however we accept this memory waste, as it's max a few 10's of kB.
    gaMemorySize = MEMORY_LOCATION_VIDEO - READ_VOLATILE(g_lastReservedAddress);
    TRACE("[pmem] [mem_init] initial size of ga memory to 0x%" PRIxIN, gaMemorySize);
    if (!IsPowerOfTwo(gaMemorySize)) {
        gaMemorySize = NextPowerOfTwo(gaMemorySize) >> 1U;
        TRACE("[pmem] [mem_init] adjusting size of ga memory to 0x%" PRIxIN, gaMemorySize);
    }

    StaticMemoryPoolConstruct(
            globalAccessMemory,
            (void*)gaMemory,
            READ_VOLATILE(g_lastReservedAddress),
            gaMemorySize,
            PAGE_SIZE);
    
    // So now we go through the memory regions provided by the system and add the physical pages
    // we can use, that are not already pre-allocated by the system.
    // ISSUE: it seems that the highest address (total number of blocks) actually
    // exceeds the number of initial blocks available
    TRACE("[pmem] [mem_init] region count %i, block count %u", bootInformation->Memory.NumberOfEntries, count);
    for (i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* entry = &bootInformation->Memory.Entries[i];
        if (entry->Type == VBootMemoryType_Available) {
            uintptr_t baseAddress = (uintptr_t)entry->PhysicalBase;
            uintptr_t limit       = (uintptr_t)entry->PhysicalBase + (uintptr_t)entry->Length;
            TRACE("[pmem] [mem_init] region %i: 0x%" PRIxIN " => 0x%" PRIxIN, i, baseAddress, limit);
            if (baseAddress < g_lastReservedAddress) {
                baseAddress = g_lastReservedAddress;
            }
            
            while (baseAddress < limit) {
                bounded_stack_push(boundedStack, (void*)baseAddress);
                baseAddress += PAGE_SIZE;
            }
        }
    }

    // Debug initial stats
    PrintPhysicalMemoryUsage();
    return OsSuccess;
}

static unsigned int
ConvertGenericAttributesToX86(
    _In_ unsigned int flags)
{
    unsigned int nativeFlags = 0;

    if (flags & MAPPING_COMMIT) {
        nativeFlags |= PAGE_PRESENT;
    }
    else {
        nativeFlags |= PAGE_RESERVED;
    }
    if (flags & MAPPING_USERSPACE) {
        nativeFlags |= PAGE_USER;
    }
    if (flags & MAPPING_NOCACHE) {
        nativeFlags |= PAGE_CACHE_DISABLE;
    }
    if (!(flags & MAPPING_READONLY)) {
        nativeFlags |= PAGE_WRITE;
    }
    if (flags & MAPPING_ISDIRTY) {
        nativeFlags |= PAGE_DIRTY;
    }
    if (flags & MAPPING_PERSISTENT) {
        nativeFlags |= PAGE_PERSISTENT;
    }
    return nativeFlags;
}

static unsigned int
ConvertX86AttributesToGeneric(
    _In_ unsigned int nativeFlags)
{
    unsigned int flags = 0;

    if (nativeFlags & (PAGE_PRESENT | PAGE_RESERVED)) {
        flags = MAPPING_EXECUTABLE; // For now
        if (nativeFlags & PAGE_PRESENT) {
            flags |= MAPPING_COMMIT;
        }
        if (!(nativeFlags & PAGE_WRITE)) {
            flags |= MAPPING_READONLY;
        }
        if (nativeFlags & PAGE_USER) {
            flags |= MAPPING_USERSPACE;
        }
        if (nativeFlags & PAGE_CACHE_DISABLE) {
            flags |= MAPPING_NOCACHE;
        }
        if (nativeFlags & PAGE_PERSISTENT) {
            flags |= MAPPING_PERSISTENT;
        }
        if (nativeFlags & PAGE_DIRTY) {
            flags |= MAPPING_ISDIRTY;
        }
    }
    return flags;
}

void
ArchMmuSwitchMemorySpace(
    _In_ MemorySpace_t* memorySpace)
{
    assert(memorySpace != NULL);
    assert(memorySpace->Data[MEMORY_SPACE_CR3] != 0);
    assert(memorySpace->Data[MEMORY_SPACE_DIRECTORY] != 0);
    memory_load_cr3(memorySpace->Data[MEMORY_SPACE_CR3]);
}

OsStatus_t
ArchMmuGetPageAttributes(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  int            pageCount,
        _In_  unsigned int*  attributeValues,
        _Out_ int*           pagesRetrievedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    int                isCurrent, update;
    unsigned int       x86Attributes;
    int                index;
    int                pagesRetrieved = 0;
    OsStatus_t         osStatus       = OsSuccess;

    if (!attributeValues || !pagesRetrievedOut) {
        return OsInvalidParameters;
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (pageTable == NULL) {
            osStatus = (pagesRetrieved == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesRetrieved++, startAddress += PAGE_SIZE) {
            x86Attributes = atomic_load(&pageTable->Pages[index]);
            attributeValues[pagesRetrieved] = ConvertX86AttributesToGeneric(x86Attributes & ATTRIBUTE_MASK);
        }
    }
    *pagesRetrievedOut = pagesRetrieved;
    return osStatus;
}

OsStatus_t
ArchMmuUpdatePageAttributes(
        _In_  MemorySpace_t*   memorySpace,
        _In_  vaddr_t startAddress,
        _In_  int              pageCount,
        _In_  unsigned int*    attributes,
        _Out_ int*             pagesUpdatedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    unsigned int       x86Attributes;
    int                isCurrent, update;
    int                index;
    int                pagesUpdated = 0;
    OsStatus_t         osStatus     = OsSuccess;

    if (!attributes || !pagesUpdatedOut) {
        return OsInvalidParameters;
    }

    x86Attributes = ConvertGenericAttributesToX86(*attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (startAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            x86Attributes |= PAGE_GLOBAL;
        }
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (pageTable == NULL) {
            osStatus = (pagesUpdated == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesUpdated++, startAddress += PAGE_SIZE) {
            uintptr_t mapping        = atomic_load(&pageTable->Pages[index]);
            uintptr_t updatedMapping = (mapping & PAGE_MASK) | x86Attributes;
            if (!pagesUpdated) {
                *attributes = ConvertX86AttributesToGeneric(mapping & ATTRIBUTE_MASK);
            }
            
            if (!atomic_compare_exchange_strong(&pageTable->Pages[index], &mapping, updatedMapping)) {
                if (isCurrent) {
                    memory_invalidate_addr(startAddress);
                }
                osStatus = (pagesUpdated == 0) ? OsBusy : OsIncomplete;
                break;
            }
            
            if (isCurrent) {
                memory_invalidate_addr(startAddress);
            }
        }
    }
    *pagesUpdatedOut = pagesUpdated;
    return osStatus;
}

OsStatus_t
ArchMmuCommitVirtualPage(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  const paddr_t* physicalAddresses,
        _In_  int            pageCount,
        _Out_ int*           pagesComittedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    int                update;
    int                isCurrent;
    int                index;
    int                pagesComitted = 0;
    OsStatus_t         osStatus      = OsSuccess;

    if (!physicalAddresses || !pagesComittedOut) {
        return OsInvalidParameters;
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount && osStatus == OsSuccess) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (!pageTable) {
            osStatus = (pagesComitted == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount > 0;
               index++, pageCount--, pagesComitted++, startAddress += PAGE_SIZE) {
            uintptr_t mapping    = atomic_load(&pageTable->Pages[index]);
            uintptr_t newMapping = ((mapping & PAGE_MASK) != 0) ?
                                   (mapping & ~(PAGE_RESERVED)) | PAGE_PRESENT :
                                   ((physicalAddresses[pagesComitted] & PAGE_MASK) | (mapping & ~(PAGE_RESERVED)) | PAGE_PRESENT);
            
            if (mapping & PAGE_PRESENT) { // Mapping was already comitted
                osStatus = (pagesComitted == 0) ? OsExists : OsIncomplete;
                break;
            }
            
            if (!(mapping & PAGE_RESERVED)) { // Mapping was not reserved
                osStatus = (pagesComitted == 0) ? OsDoesNotExist : OsIncomplete;
                break;
            }
            
            if (!atomic_compare_exchange_strong(&pageTable->Pages[index], &mapping, newMapping)) {
                WARNING("[arch_commit_virtual] address 0x%" PRIxIN ", was updated before us to 0x%" PRIxIN,
                      startAddress, mapping);
                if (isCurrent) {
                    memory_invalidate_addr(startAddress);
                }
                osStatus = (pagesComitted == 0) ? OsExists : OsIncomplete;
                break;
            }
            
            if (isCurrent) {
                memory_invalidate_addr(startAddress);
            }
        }
    }
    *pagesComittedOut = pagesComitted;
    return osStatus;
}

OsStatus_t
ArchMmuSetContiguousVirtualPages(
        _In_  MemorySpace_t*    memorySpace,
        _In_  vaddr_t  startAddress,
        _In_  paddr_t physicalStartAddress,
        _In_  int               pageCount,
        _In_  unsigned int      attributes,
        _Out_ int*              pagesUpdatedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    unsigned int       x86Attributes;
    int                update;
    int                isCurrent;
    int                index;
    int                pagesUpdated = 0;
    OsStatus_t         status       = OsSuccess;
    uintptr_t          zero         = 0;

    if (!pagesUpdatedOut) {
        return OsInvalidParameters;
    }

    x86Attributes = ConvertGenericAttributesToX86(attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (startAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            x86Attributes |= PAGE_GLOBAL;
        }
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount && status == OsSuccess) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 1, &update);
        if (!pageTable) {
            status = (pagesUpdated == 0) ? OsOutOfMemory : OsIncomplete;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount;
               index++, pageCount--, pagesUpdated++, startAddress += PAGE_SIZE, physicalStartAddress += PAGE_SIZE) {
            uintptr_t mapping = (physicalStartAddress & PAGE_MASK) | x86Attributes;
            if (!atomic_compare_exchange_strong(&pageTable->Pages[index], &zero, mapping)) {
                // Tried to replace a value that was not 0
                WARNING("[arch_update_virtual_cont] failed to map 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
                      startAddress, zero);
                if (isCurrent) {
                    memory_invalidate_addr(startAddress);
                }
                status = (pagesUpdated == 0) ? OsExists : OsIncomplete;
                break;
            }
            
            if (isCurrent) {
                memory_invalidate_addr(startAddress);
            }
        }
    }
    *pagesUpdatedOut = pagesUpdated;
    return status;
}

OsStatus_t
ArchMmuReserveVirtualPages(
        _In_  MemorySpace_t*   memorySpace,
        _In_  vaddr_t startAddress,
        _In_  int              pageCount,
        _In_  unsigned int     attributes,
        _Out_ int*             pagesReservedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    unsigned int       x86Attributes;
    int                update;
    int                isCurrent;
    int                index;
    int                pagesReserved = 0;
    OsStatus_t         status        = OsSuccess;
    uintptr_t          zero          = 0;

    if (!pagesReservedOut) {
        return OsInvalidParameters;
    }

    x86Attributes = ConvertGenericAttributesToX86(attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (startAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            x86Attributes |= PAGE_GLOBAL;
        }
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount && status == OsSuccess) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 1, &update);
        if (!pageTable) {
            status = (pagesReserved == 0) ? OsOutOfMemory : OsIncomplete;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesReserved++, startAddress += PAGE_SIZE) {
            if (!atomic_compare_exchange_strong(&pageTable->Pages[index], &zero, x86Attributes)) {
                // Tried to replace a value that was not 0
                WARNING("[arch_reserve_virtual] failed to reserve address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
                      startAddress, zero);
                status = OsIncomplete;
                break;
            }
        }
    }
    *pagesReservedOut = pagesReserved;
    return status;
}

OsStatus_t
ArchMmuSetVirtualPages(
        _In_  MemorySpace_t*           memorySpace,
        _In_  vaddr_t         startAddress,
        _In_  const paddr_t* physicalAddressValues,
        _In_  int                      pageCount,
        _In_  unsigned int             attributes,
        _Out_ int*                     pagesUpdatedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    unsigned int       x86Attributes;
    int                update;
    int                isCurrent;
    int                index;
    int                pagesUpdated = 0;
    OsStatus_t         status       = OsSuccess;
    uintptr_t          zero         = 0;

    if (!physicalAddressValues || !pagesUpdatedOut) {
        return OsInvalidParameters;
    }

    x86Attributes = ConvertGenericAttributesToX86(attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (startAddress < MEMORY_LOCATION_KERNEL_END) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OsSuccess) {
            x86Attributes |= PAGE_GLOBAL;
        }
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount && status == OsSuccess) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 1, &update);
        if (!pageTable) {
            status = (pagesUpdated == 0) ? OsOutOfMemory : OsIncomplete;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesUpdated++, startAddress += PAGE_SIZE) {
            uintptr_t mapping = (physicalAddressValues[pagesUpdated] & PAGE_MASK) | x86Attributes;
            if (!atomic_compare_exchange_strong(&pageTable->Pages[index], &zero, mapping)) {
                // Tried to replace a value that was not 0
                ERROR("[arch_update_virtual] failed to update address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
                      startAddress, zero);
                if (isCurrent) {
                    memory_invalidate_addr(startAddress);
                }
                status = OsIncomplete;
                break;
            }
            
            if (isCurrent) {
                memory_invalidate_addr(startAddress);
            }
        }
    }
    *pagesUpdatedOut = pagesUpdated;
    return status;
}

OsStatus_t
ArchMmuClearVirtualPages(
        _In_  MemorySpace_t*     memorySpace,
        _In_  vaddr_t   startAddress,
        _In_  int                pageCount,
        _In_  paddr_t* freedAddresses,
        _Out_ int*               freedAddressesCountOut,
        _Out_ int*               pagesClearedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    uintptr_t          mapping;
    int                update;
    int                isCurrent;
    int                index;
    int                pagesCleared = 0;
    int                freedPages   = 0;
    OsStatus_t         status       = OsSuccess;

    if (!freedAddresses || !freedAddressesCountOut || !pagesClearedOut) {
        return OsInvalidParameters;
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (pageTable == NULL) {
            status = (pagesCleared == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesCleared++, startAddress += PAGE_SIZE) {
            mapping = atomic_exchange(&pageTable->Pages[index], 0);
            
            // Release memory, but don't if it is a virtual mapping, that means we 
            // should not free the physical page. We only do this if the memory
            // is marked as present, otherwise we don't
            if (mapping & PAGE_PRESENT) {
                if (!(mapping & PAGE_PERSISTENT)) {
                    freedAddresses[freedPages++] = mapping & PAGE_MASK;
                }

                if (isCurrent) {
                    memory_invalidate_addr(startAddress);
                }
            }
        }
    }
    *freedAddressesCountOut = freedPages;
    *pagesClearedOut = pagesCleared;
    return status;
}

OsStatus_t
ArchMmuVirtualToPhysical(
        _In_  MemorySpace_t*     memorySpace,
        _In_  vaddr_t   startAddress,
        _In_  int                pageCount,
        _In_  paddr_t* physicalAddressValues,
        _Out_ int*               pagesRetrievedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    uint32_t           mapping;
    int                isCurrent, update;
    int                index;
    int                pagesRetrieved = 0;
    OsStatus_t         status         = OsSuccess;

    if (!physicalAddressValues || !pagesRetrievedOut) {
        return OsInvalidParameters;
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (pageTable == NULL) {
            status = (pagesRetrieved == 0) ? OsDoesNotExist : OsIncomplete;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesRetrieved++, startAddress += PAGE_SIZE) {
            mapping = atomic_load(&pageTable->Pages[index]);
            mapping &= PAGE_MASK;
            if (!pagesRetrieved) {
                mapping |= startAddress & ATTRIBUTE_MASK;
            }
            physicalAddressValues[pagesRetrieved] = mapping;
        }
    }
    *pagesRetrievedOut = pagesRetrieved;
    return status;
}

OsStatus_t
SetDirectIoAccess(
        _In_ UUId_t         coreId,
        _In_ MemorySpace_t* memorySpace,
        _In_ uint16_t       port,
        _In_ int            enable)
{
    uint8_t* ioMap = (uint8_t*)memorySpace->Data[MEMORY_SPACE_IOMAP];
    if (!ioMap) {
        return OsInvalidParameters;
    }

    // Update thread's io-map and the active access
    if (enable) {
        ioMap[port / 8] &= ~(1 << (port % 8));
        if (coreId == ArchGetProcessorCoreId()) {
            TssEnableIo(coreId, port);
        }
    }
    else {
        ioMap[port / 8] |= (1 << (port % 8));
        if (coreId == ArchGetProcessorCoreId()) {
            TssDisableIo(coreId, port);
        }
    }
    return OsSuccess;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
