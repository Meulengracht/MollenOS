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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Memory Utility Functions
 *   - Implements helpers and utility functions with the MemoryInitialize.
 */

#define __MODULE "MEM0"
//#define __TRACE

#include <arch/mmu.h>
#include <arch/output.h>
#include <arch/utils.h>
#include <arch/x86/arch.h>
#include <arch/x86/cpu.h>
#include <arch/x86/memory.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <machine.h>
#include <os/types/shm.h>
#include <string.h>

#if defined(__i386__)
#include <arch/x86/x32/gdt.h>
#else
#include <arch/x86/x64/gdt.h>
#endif

// Interface to the arch-specific
extern void memory_invalidate_addr(uintptr_t pda);
extern void memory_load_cr3(uintptr_t pda);
extern void memory_reload_cr3(void);
extern void memory_paging_init(uintptr_t pda, paddr_t stackPhysicalBase, vaddr_t stackVirtualBase);

extern uintptr_t g_kernelcr3;
extern uintptr_t g_kernelpd;

// Disable the atomic wrong alignment, as they are aligned and are sanitized
// in the arch-specific layer
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Watomic-alignment"
#endif

void
MmuGetMemoryConfiguration(
        _In_ PlatformMemoryConfiguration_t* configuration)
{
    configuration->PageSize = PAGE_SIZE;

    configuration->MemoryMap.Shared.Start  = MEMORY_LOCATION_SHARED_START;
    configuration->MemoryMap.Shared.Length = MEMORY_LOCATION_SHARED_END - MEMORY_LOCATION_SHARED_START;

    configuration->MemoryMap.UserCode.Start  = MEMORY_LOCATION_RING3_CODE;
    configuration->MemoryMap.UserCode.Length = MEMORY_LOCATION_RING3_CODE_END - MEMORY_LOCATION_RING3_CODE;

    configuration->MemoryMap.UserHeap.Start  = MEMORY_LOCATION_RING3_HEAP;
    configuration->MemoryMap.UserHeap.Length = MEMORY_LOCATION_RING3_HEAP_END - MEMORY_LOCATION_RING3_HEAP;

    configuration->MemoryMap.ThreadLocal.Start  = MEMORY_LOCATION_RING3_THREAD_START;
    configuration->MemoryMap.ThreadLocal.Length = MEMORY_LOCATION_RING3_THREAD_END - MEMORY_LOCATION_RING3_THREAD_START;

    // initialize masks
    // 1MB - BIOS
    // 16MB - ISA
    // 2GB - 32 bit drivers (for broken devices)
    // 4GB - 32 bit drivers
    // rest
    configuration->MemoryMaskCount = 0;
    configuration->MemoryMasks[configuration->MemoryMaskCount++] = MEMORY_MASK_BIOS;
    configuration->MemoryMasks[configuration->MemoryMaskCount++] = MEMORY_MASK_ISA;
    configuration->MemoryMasks[configuration->MemoryMaskCount++] = MEMORY_MASK_2GB;
    configuration->MemoryMasks[configuration->MemoryMaskCount++] = MEMORY_MASK_32BIT;
#if defined(__amd64__)
    configuration->MemoryMasks[configuration->MemoryMaskCount++] = MEMORY_MASK_64BIT;
#endif
}


oserr_t
ArchSHMTypeToPageMask(
        _In_  unsigned int dmaType,
        _Out_ size_t*      pageMaskOut)
{
    switch (dmaType) {
#if defined(__amd64__)
        case SHM_TYPE_REGULAR:      *pageMaskOut = MEMORY_MASK_64BIT; return OS_EOK;
#else
        case SHM_TYPE_REGULAR:      *pageMaskOut = MEMORY_MASK_32BIT; return OS_EOK;
#endif
        case SHM_TYPE_DRIVER_ISA:   *pageMaskOut = MEMORY_MASK_ISA;   return OS_EOK;
        case SHM_TYPE_DRIVER_32LOW: *pageMaskOut = MEMORY_MASK_2GB;   return OS_EOK;
        case SHM_TYPE_DRIVER_32:    *pageMaskOut = MEMORY_MASK_32BIT; return OS_EOK;

#if defined(__amd64__)
        case SHM_TYPE_DRIVER_64:    *pageMaskOut = MEMORY_MASK_64BIT; return OS_EOK;
#else
        case SHM_TYPE_DRIVER_64:    *pageMaskOut = MEMORY_MASK_32BIT; return OS_EOK;
#endif

        default:
            return OS_ENOTSUPPORTED;
    }
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
    assert(memorySpace->PlatformData.Cr3PhysicalAddress != 0);
    assert(memorySpace->PlatformData.Cr3VirtualAddress != 0);
    memory_load_cr3(memorySpace->PlatformData.Cr3PhysicalAddress);
}

oserr_t
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
    oserr_t         osStatus       = OS_EOK;

    if (!attributeValues || !pagesRetrievedOut) {
        return OS_EINVALPARAMS;
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (pageTable == NULL) {
            osStatus = (pagesRetrieved == 0) ? OS_ENOENT : OS_EINCOMPLETE;
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

oserr_t
ArchMmuUpdatePageAttributes(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  int            pageCount,
        _In_  unsigned int*  attributes,
        _Out_ int*           pagesUpdatedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    unsigned int       x86Attributes;
    int                isCurrent, update;
    int                index;
    int                pagesUpdated = 0;
    oserr_t         osStatus     = OS_EOK;

    if (!attributes || !pagesUpdatedOut) {
        return OS_EINVALPARAMS;
    }

    x86Attributes = ConvertGenericAttributesToX86(*attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (ISINRANGE(startAddress, MEMORY_LOCATION_SHARED_START, MEMORY_LOCATION_SHARED_END)) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OS_EOK) {
            x86Attributes |= PAGE_GLOBAL;
        }
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (pageTable == NULL) {
            osStatus = (pagesUpdated == 0) ? OS_ENOENT : OS_EINCOMPLETE;
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
                osStatus = (pagesUpdated == 0) ? OS_EBUSY : OS_EINCOMPLETE;
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

oserr_t
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
    oserr_t         osStatus      = OS_EOK;

    if (!physicalAddresses || !pagesComittedOut) {
        return OS_EINVALPARAMS;
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount && osStatus == OS_EOK) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (!pageTable) {
            osStatus = (pagesComitted == 0) ? OS_ENOENT : OS_EINCOMPLETE;
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
                osStatus = (pagesComitted == 0) ? OS_EEXISTS : OS_EINCOMPLETE;
                break;
            }
            
            if (!(mapping & PAGE_RESERVED)) { // Mapping was not reserved
                osStatus = (pagesComitted == 0) ? OS_ENOENT : OS_EINCOMPLETE;
                break;
            }
            
            if (!atomic_compare_exchange_strong(&pageTable->Pages[index], &mapping, newMapping)) {
                WARNING("[arch_commit_virtual] address 0x%" PRIxIN ", was updated before us to 0x%" PRIxIN,
                      startAddress, mapping);
                if (isCurrent) {
                    memory_invalidate_addr(startAddress);
                }
                osStatus = (pagesComitted == 0) ? OS_EEXISTS : OS_EINCOMPLETE;
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

oserr_t
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
    oserr_t         status       = OS_EOK;
    uintptr_t          zero         = 0;

    if (!pagesUpdatedOut) {
        return OS_EINVALPARAMS;
    }

    x86Attributes = ConvertGenericAttributesToX86(attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (ISINRANGE(startAddress, MEMORY_LOCATION_SHARED_START, MEMORY_LOCATION_SHARED_END)) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OS_EOK) {
            x86Attributes |= PAGE_GLOBAL;
        }
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount && status == OS_EOK) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 1, &update);
        if (!pageTable) {
            status = (pagesUpdated == 0) ? OS_EOOM : OS_EINCOMPLETE;
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
                status = (pagesUpdated == 0) ? OS_EEXISTS : OS_EINCOMPLETE;
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

oserr_t
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
    oserr_t            status        = OS_EOK;
    uintptr_t          zero          = 0;

    if (!pagesReservedOut) {
        return OS_EINVALPARAMS;
    }

    x86Attributes = ConvertGenericAttributesToX86(attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (ISINRANGE(startAddress, MEMORY_LOCATION_SHARED_START, MEMORY_LOCATION_SHARED_END)) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OS_EOK) {
            x86Attributes |= PAGE_GLOBAL;
        }
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount && status == OS_EOK) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 1, &update);
        if (!pageTable) {
            status = (pagesReserved == 0) ? OS_EOOM : OS_EINCOMPLETE;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesReserved++, startAddress += PAGE_SIZE) {
            if (!atomic_compare_exchange_strong(&pageTable->Pages[index], &zero, x86Attributes)) {
                // Tried to replace a value that was not 0
                WARNING("[arch_reserve_virtual] failed to reserve address 0x%" PRIxIN ", existing mapping was in place 0x%" PRIxIN,
                      startAddress, zero);
                status = OS_EINCOMPLETE;
                break;
            }
        }
    }
    *pagesReservedOut = pagesReserved;
    return status;
}

oserr_t
ArchMmuSetVirtualPages(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  const paddr_t* physicalAddressValues,
        _In_  int            pageCount,
        _In_  unsigned int   attributes,
        _Out_ int*           pagesUpdatedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    unsigned int       x86Attributes;
    int                update;
    int                isCurrent;
    int                index;
    int                pagesUpdated = 0;
    oserr_t         status       = OS_EOK;
    uintptr_t          zero         = 0;

    if (!physicalAddressValues || !pagesUpdatedOut) {
        return OS_EINVALPARAMS;
    }

    x86Attributes = ConvertGenericAttributesToX86(attributes);
    
    // For kernel mappings we would like to mark the mappings global
    if (ISINRANGE(startAddress, MEMORY_LOCATION_SHARED_START, MEMORY_LOCATION_SHARED_END)) {
        if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OS_EOK) {
            x86Attributes |= PAGE_GLOBAL;
        }
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount && status == OS_EOK) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 1, &update);
        if (!pageTable) {
            status = (pagesUpdated == 0) ? OS_EOOM : OS_EINCOMPLETE;
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
                status = OS_EINCOMPLETE;
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

oserr_t
ArchMmuClearVirtualPages(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  int            pageCount,
        _In_  paddr_t*       freedAddresses,
        _Out_ int*           freedAddressesCountOut,
        _Out_ int*           pagesClearedOut)
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
    oserr_t            status       = OS_EOK;

    if (!freedAddressesCountOut || !pagesClearedOut) {
        return OS_EINVALPARAMS;
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (pageTable == NULL) {
            status = (pagesCleared == 0) ? OS_ENOENT : OS_EINCOMPLETE;
            break;
        }

        index = PAGE_TABLE_INDEX(startAddress);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesCleared++, startAddress += PAGE_SIZE) {
            mapping = atomic_exchange(&pageTable->Pages[index], 0);
            
            // Release memory, but don't if it is a virtual mapping, that means we 
            // should not free the physical page. We only do this if the memory
            // is marked as present, otherwise we don't
            if (mapping & PAGE_PRESENT) {
                if (freedAddresses && !(mapping & PAGE_PERSISTENT)) {
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

oserr_t
ArchMmuVirtualToPhysical(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        startAddress,
        _In_  int            pageCount,
        _In_  paddr_t*       physicalAddressValues,
        _Out_ int*           pagesRetrievedOut)
{
    PAGE_MASTER_LEVEL* parentDirectory;
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    uintptr_t          mapping;
    int                isCurrent, update;
    int                index;
    int                pagesRetrieved = 0;
    oserr_t         status         = OS_EOK;

    if (!physicalAddressValues || !pagesRetrievedOut) {
        return OS_EINVALPARAMS;
    }

    directory = MmVirtualGetMasterTable(memorySpace, startAddress, &parentDirectory, &isCurrent);
    while (pageCount) {
        pageTable = MmVirtualGetTable(parentDirectory, directory, startAddress, isCurrent, 0, &update);
        if (pageTable == NULL) {
            status = (pagesRetrieved == 0) ? OS_ENOENT : OS_EINCOMPLETE;
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

static oserr_t
__InitializePlatformMemoryData(
        _In_ MemorySpace_t* memorySpace)
{
    memorySpace->PlatformData.TssIoMap = kmalloc(GDT_IOMAP_SIZE);
    if (!memorySpace->PlatformData.TssIoMap) {
        return OS_EOOM;
    }

    if (memorySpace->Flags & MEMORY_SPACE_APPLICATION) {
        memset(memorySpace->PlatformData.TssIoMap, 0xFF, GDT_IOMAP_SIZE);
    }
    else {
        memset(memorySpace->PlatformData.TssIoMap, 0, GDT_IOMAP_SIZE);
    }
    return OS_EOK;
}

oserr_t
MmuCloneVirtualSpace(
        _In_ MemorySpace_t* parent,
        _In_ MemorySpace_t* child,
        _In_ int            inherit)
{
    oserr_t osStatus;
    paddr_t    physicalAddress;
    vaddr_t    virtualAddress;

    osStatus = MmVirtualClone(parent, inherit, &physicalAddress, &virtualAddress);
    if (osStatus != OS_EOK) {
        return osStatus;
    }
    TRACE("MmuCloneVirtualSpace cr3=0x%llx (virt=0x%llx)", physicalAddress, virtualAddress);

    // Update the configuration data for the memory space
    child->PlatformData.Cr3PhysicalAddress = physicalAddress;
    child->PlatformData.Cr3VirtualAddress  = virtualAddress;

    // Install the TLS mapping immediately and have it ready for thread switch
    virtualAddress = MEMORY_LOCATION_TLS_START;
    osStatus       = MemorySpaceMap(
            child,
            &(struct MemorySpaceMapOptions) {
                .Pages = &physicalAddress,
                .Length = PAGE_SIZE,
                .Mask = __MASK,
                .Flags = MAPPING_COMMIT,
                .PlacementFlags = MAPPING_VIRTUAL_FIXED
            },
            &virtualAddress
    );
    if (osStatus != OS_EOK) {
        MmuDestroyVirtualSpace(child);
        return osStatus;
    }

    // Create new resources for the happy new parent :-)
    if (!parent) {
        osStatus = __InitializePlatformMemoryData(child);
        if (osStatus != OS_EOK) {
            MmuDestroyVirtualSpace(child);
            return osStatus;
        }
    }
    return OS_EOK;
}

oserr_t
SetDirectIoAccess(
        _In_ uuid_t         coreId,
        _In_ MemorySpace_t* memorySpace,
        _In_ uint16_t       port,
        _In_ int            enable)
{
    SystemCpuCore_t* cpuCore = CpuCoreCurrent();
    uint8_t*         ioMap   = (uint8_t*)memorySpace->PlatformData.TssIoMap;
    if (!ioMap) {
        return OS_EINVALPARAMS;
    }

    // Update thread's io-map and the active access
    if (enable) {
        ioMap[port / 8] &= ~(1 << (port % 8));
        if (coreId == CpuCoreId(cpuCore)) {
            TssEnableIo(CpuCorePlatformBlock(cpuCore), port);
        }
    }
    else {
        ioMap[port / 8] |= (1 << (port % 8));
        if (coreId == CpuCoreId(cpuCore)) {
            TssDisableIo(CpuCorePlatformBlock(cpuCore), port);
        }
    }
    return OS_EOK;
}

static unsigned int
__GetBootMappingAttributes(
        _In_ unsigned int attributes)
{
    unsigned int converted = PAGE_PRESENT | PAGE_PERSISTENT;
    if (!(attributes & VBOOT_MEMORY_RO)) {
        converted |= PAGE_WRITE;
    }
    return converted;
}

static oserr_t
__InstallFirmwareMapping(
        _In_  MemorySpace_t* memorySpace,
        _In_  vaddr_t        virtualBase,
        _In_  paddr_t        physicalBase,
        _In_  int            pageCount,
        _In_  unsigned int   attributes)
{
    PAGE_MASTER_LEVEL* directory;
    PageTable_t*       pageTable;
    int                index;
    int                pagesUpdated = 0;
    oserr_t         status       = OS_EOK;
    TRACE("__InstallFirmwareMapping(virtualBase=0x%" PRIxIN", physicalBase=0x%" PRIxIN ", pageCount=%i)",
          virtualBase, physicalBase, pageCount);

    if (CpuHasFeatures(0, CPUID_FEAT_EDX_PGE) == OS_EOK) {
        attributes |= PAGE_GLOBAL;
    }

    directory = (PAGE_MASTER_LEVEL*)memorySpace->PlatformData.Cr3PhysicalAddress;
    while (pageCount) {
        pageTable = MmBootGetPageTable(directory, virtualBase);
        if (!pageTable) {
            ERROR("__InstallFirmwareMapping pagetable for address 0x%" PRIxIN " was not found", virtualBase);
            status = OS_EUNKNOWN;
            break;
        }

        index = PAGE_TABLE_INDEX(virtualBase);
        for (; index < ENTRIES_PER_PAGE && pageCount; index++, pageCount--, pagesUpdated++, virtualBase += PAGE_SIZE) {
            uintptr_t mapping = ((physicalBase + (pagesUpdated * PAGE_SIZE)) & PAGE_MASK) | attributes;
            atomic_store(&pageTable->Pages[index], mapping);
        }
    }
    return status;
}

static oserr_t
__CreateFirmwareMapping(
        _In_ MemorySpace_t*           memorySpace,
        _In_ struct VBootMemoryEntry* entry)
{
    vaddr_t    virtualBase;
    oserr_t osStatus;
    int        pageCount;
    TRACE("__CreateFirmwareMapping(physicalBase=0x%" PRIxIN ", length=0x%" PRIxIN ")",
          entry->PhysicalBase, entry->Length);

    // Allocate a new virtual mapping
    virtualBase = StaticMemoryPoolAllocate(&GetMachine()->GlobalAccessMemory, entry->Length);
    if (virtualBase == 0) {
        ERROR("__CreateFirmwareMapping Ran out of memory for allocation 0x%" PRIxIN " (ga-memory)", entry->Length);
        return OS_EOOM;
    }

    pageCount = DIVUP(entry->Length, PAGE_SIZE);

    // Create the new mapping in the address space
    osStatus = __InstallFirmwareMapping(memorySpace,
                           virtualBase,
                           entry->PhysicalBase,
                           pageCount,
                           __GetBootMappingAttributes(entry->Attributes)
    );
    if (osStatus == OS_EOK) {
        entry->VirtualBase = virtualBase;
    }
    return osStatus;
}

static oserr_t
__HandleFirmwareMappings(
        _In_  MemorySpace_t* memorySpace,
        _In_  struct VBoot*  bootInformation,
        _Out_ uintptr_t*     stackMapping)
{
    struct VBootMemoryEntry* entries;
    unsigned int             i;
    TRACE("__HandleFirmwareMappings()");

    entries = (struct VBootMemoryEntry*)bootInformation->Memory.Entries;
    for (i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* entry = &entries[i];
        if (entry->Type == VBootMemoryType_Firmware) {
            oserr_t osStatus = __CreateFirmwareMapping(memorySpace, entry);
            if (osStatus != OS_EOK) {
                return osStatus;
            }

            // Store the stack mapping
            if ((uintptr_t)bootInformation->Stack.Base == entry->PhysicalBase) {
                *stackMapping = entry->VirtualBase;
            }
        }
    }
    return OS_EOK;
}

static oserr_t
__RemapFramebuffer(
        _In_ MemorySpace_t* memorySpace)
{
    vaddr_t    virtualBase;
    size_t     framebufferSize;
    oserr_t osStatus;
    int        pageCount;
    TRACE("__RemapFramebuffer(framebuffer=0x%" PRIxIN ")",
          VideoGetTerminal()->FrameBufferAddress);

    // If no framebuffer for output, no need to do this
    if (!VideoGetTerminal()->FrameBufferAddress) {
        return OS_EOK;
    }

    framebufferSize = VideoGetTerminal()->Info.BytesPerScanline * VideoGetTerminal()->Info.Height;

    // Allocate a new virtual mapping
    virtualBase = StaticMemoryPoolAllocate(&GetMachine()->GlobalAccessMemory, framebufferSize);
    if (virtualBase == 0) {
        ERROR("__RemapFramebuffer Ran out of memory for allocation 0x%" PRIxIN " (ga-memory)", framebufferSize);
        return OS_EOOM;
    }

    pageCount = DIVUP(framebufferSize, PAGE_SIZE);

    // Create the new mapping in the address space
    osStatus = __InstallFirmwareMapping(memorySpace,
                                        virtualBase,
                                        VideoGetTerminal()->FrameBufferAddress,
                                        pageCount,
                                        PAGE_PRESENT | PAGE_WRITE | PAGE_USER | PAGE_PERSISTENT
    );

    // Update video address to the new
    VideoGetTerminal()->FrameBufferAddress = virtualBase;
    return osStatus;
}

static vaddr_t
__GetVirtualMapping(
        _In_ struct VBoot* bootInformation,
        _In_ paddr_t       physicalBase)
{
    struct VBootMemoryEntry* entries;

    TRACE("__GetVirtualMapping(physicalBase=0x%" PRIxIN ")", physicalBase);
    entries = (struct VBootMemoryEntry*)bootInformation->Memory.Entries;
    for (unsigned int i = 0; i < bootInformation->Memory.NumberOfEntries; i++) {
        struct VBootMemoryEntry* entry = &entries[i];
        if (ISINRANGE(physicalBase, entry->PhysicalBase, entry->PhysicalBase + entry->Length)) {
            TRACE("__GetVirtualMapping entry->PhysicalBase=0x%" PRIxIN " entry->VirtualBase=0x%" PRIxIN,
                  entry->PhysicalBase, entry->VirtualBase);
            TRACE("__GetVirtualMapping return=0x%" PRIxIN,
                  entry->VirtualBase + (physicalBase - entry->PhysicalBase));
            return entry->VirtualBase + (physicalBase - entry->PhysicalBase);
        }
    }
    TRACE("__GetVirtualMapping not found");
    return 0;
}

static void
__FixupVBootAddresses(
        _In_ struct VBoot* bootInformation)
{
    TRACE("__FixupVBootAddresses()");

    // Update configuration table and entries if present
    if (bootInformation->ConfigurationTableCount) {
        bootInformation->ConfigurationTable = __GetVirtualMapping(
                bootInformation,
                (paddr_t)bootInformation->ConfigurationTable);
    }

    // Update ramdisk
    bootInformation->Ramdisk.Data = __GetVirtualMapping(
            bootInformation,
            (paddr_t)bootInformation->Ramdisk.Data);

    // Update stack
    bootInformation->Stack.Base = __GetVirtualMapping(
            bootInformation,
            (paddr_t)bootInformation->Stack.Base);
}

static oserr_t
__CreateKernelMappings(
        _In_ MemorySpace_t*           memorySpace,
        _In_ PlatformMemoryMapping_t* kernelMappings)
{
    PlatformMemoryMapping_t* i;
    TRACE("__CreateKernelMappings()");

    i = kernelMappings;
    while (i->Length) {
        int        pageCount = (int)DIVUP(i->Length, PAGE_SIZE);
        oserr_t osStatus = __InstallFirmwareMapping(
                memorySpace,
                i->VirtualBase,
                i->PhysicalBase,
                pageCount,
                PAGE_PRESENT | PAGE_WRITE
        );
        if (osStatus != OS_EOK) {
            return osStatus;
        }

        // go to next entry, the kernel mappings end with a 0 entry
        i++;
    }
    return OS_EOK;
}

static oserr_t
__InitializeTLS(
        _In_ MemorySpace_t* memorySpace)
{
    uintptr_t  tlsPhysical;
    oserr_t osStatus;
    TRACE("__InitializeTLS()");

    osStatus = MachineAllocateBootMemory(
            PAGE_SIZE,
            NULL,
            (paddr_t*)&tlsPhysical
    );
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    return __InstallFirmwareMapping(
            memorySpace,
            MEMORY_LOCATION_TLS_START,
            tlsPhysical,
            1,
            PAGE_PRESENT | PAGE_WRITE
    );
}

oserr_t
MmuLoadKernel(
        _In_ MemorySpace_t*           memorySpace,
        _In_ struct VBoot*            bootInformation,
        _In_ PlatformMemoryMapping_t* kernelMappings)
{
    TRACE("MmuLoadKernel()");

    uintptr_t  stackVirtual;
    uintptr_t  stackPhysical;
    oserr_t osStatus;

    // Create the system kernel virtual memory space, this call identity maps all
    // memory allocated by AllocateBootMemory, and also allocates some itself
    MmBootPrepareKernel();

    // Update the configuration data for the memory space
    memorySpace->PlatformData.Cr3PhysicalAddress = g_kernelcr3;
    memorySpace->PlatformData.Cr3VirtualAddress  = g_kernelpd;
    memorySpace->PlatformData.TssIoMap           = NULL;        // fill this in later when we allocate tss

    // store the physical base of the stack
    stackPhysical = (uintptr_t)bootInformation->Stack.Base;

    // Install the TLS mapping for the boot thread
    osStatus = __InitializeTLS(memorySpace);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Install any remaining virtual mappings before we enable it. Currently, firmware
    // mappings still need to be allocated into global access memory. We also need to ensure
    // that the kernel stack is mapped correctly
    osStatus = __HandleFirmwareMappings(memorySpace, bootInformation, &stackVirtual);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Remap the framebuffer for good times
    osStatus = __RemapFramebuffer(memorySpace);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Create all the kernel mappings
    osStatus = __CreateKernelMappings(memorySpace, kernelMappings);
    if (osStatus != OS_EOK) {
        return osStatus;
    }

    // Update all mappings in vboot to point to virtual ones instead of physical
    __FixupVBootAddresses(bootInformation);

    // initialize paging and swap the stack address to virtual one, so we don't crap out.
    TRACE("MmuLoadKernel g_kernelcr3=0x%" PRIxIN ", stackPhysical=0x%" PRIxIN ", stackVirtual=0x%" PRIxIN,
          g_kernelcr3, stackPhysical, stackVirtual);
    memory_paging_init(g_kernelcr3, stackPhysical, stackVirtual);

    return OS_EOK;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
