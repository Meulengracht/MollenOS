/**
 * Copyright, Philip Meulengracht
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
 * ARM SMMU Stage-2 Page Tables
 * Shared page-table allocation and entry management for SMMUv3 and MMU-500.
 * Reserves every directory page a mapping might need up front so writing
 * final page entries never has to allocate memory.
 */

#include <debug.h>
#include <heap.h>
#include <limits.h>
#include <string.h>

#include "stage2-table.h"

// Tracks one allocated 4 KiB page-table block. Using one heap wrapper per page
// lets __LinkSparePage move a page from a mapping's SparePages list directly
// into table->Pages without another allocation.
typedef struct ArmSMMUStage2Page {
    element_t            Link;
    ArmSMMUMemoryBlock_t Block;
} ArmSMMUStage2Page_t;

// Notifies the backend that table consistency broke after logging the cause.
static oserr_t
__FailTable(
    _In_ const ArmSMMUStage2Table_t* table,
    _In_ oserr_t                     status)
{
    table->Unit->Fail(table->Unit->Context, status);
    return status;
}

// Frees every page in pages back to the control pool. If returning a page
// fails, that page stays in the list and the unit is marked failed so the
// pool can never be reused.
static oserr_t
__FreePages(
    _In_ const ArmSMMUStage2Table_t* table,
    _In_ list_t*                     pages)
{
    const ArmSMMUStage2Unit_t* unit = table->Unit;
    ArmSMMUStage2Page_t*       page;
    oserr_t                    status;

    while (pages->head != NULL) {
        page = pages->head->value;
        
        status = ArmSMMUMemoryFree(unit->Memory, &page->Block);
        if (status != OS_EOK) {
            ERROR("__FreePages: freeing page %llx at %llx failed: %u",
                  (uint64_t)page->Block.PhysicalAddress, unit->Description->RegisterBase, status);
            return __FailTable(table, status);
        }

        if (list_remove(pages, &page->Link) != 0) {
            ERROR("__FreePages: unlinking page at %llx failed", unit->Description->RegisterBase);
            return __FailTable(table, OS_EUNKNOWN);
        }
        kfree(page);
    }
    return OS_EOK;
}

oserr_t
ArmSMMUStage2TableCreate(
    _In_ ArmSMMUStage2Table_t*      table,
    _In_ const ArmSMMUStage2Unit_t* unit)
{
    oserr_t status;

    table->Unit = unit;
    list_construct(&table->Pages);
    
    status = ArmSMMUMemoryAllocate(
        unit->Memory,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        ARM_SMMU_MEMORY_PAGE_SIZE,
        &table->Root
    );
    if (status != OS_EOK) {
        if (unit->Memory->State == ArmSMMUMemoryFailed) {
            ERROR("ArmSMMUStage2TableCreate: root table allocation at %llx failed: %u",
                  unit->Description->RegisterBase, status);
            __FailTable(table, status);
        }
        return status;
    }
    return OS_EOK;
}

oserr_t
ArmSMMUStage2TableDestroy(
    _In_ ArmSMMUStage2Table_t* table)
{
    const ArmSMMUStage2Unit_t* unit = table->Unit;
    oserr_t                    status;

    status = __FreePages(table, &table->Pages);
    if (status != OS_EOK) {
        return status;
    }

    status = ArmSMMUMemoryFree(unit->Memory, &table->Root);
    if (status != OS_EOK) {
        ERROR("ArmSMMUStage2TableDestroy: freeing root table at %llx failed: %u",
              unit->Description->RegisterBase, status);
        return __FailTable(table, status);
    }
    return OS_EOK;
}

// Validates one physical scatter-gather chunk: it must be non-empty, 4 KiB
// aligned, reachable by this SMMU, and fit within the remaining mapping size.
static oserr_t
__ValidateExtent(
    _In_ const ArmSMMUStage2Table_t* table,
    _In_ const SHMSG_t*              extent,
    _In_ uint64_t                    remaining)
{
    const ArmSMMUStage2Unit_t* unit = table->Unit;
    uint64_t                   address = extent->Address;
    uint64_t                   length = extent->Length;

    if (length == 0 || address % ARM_SMMU_S2_PAGE_SIZE != 0 || length % ARM_SMMU_S2_PAGE_SIZE != 0) {
        WARNING("__ValidateExtent: extent %llx, length %llx is empty or not page aligned", address, length);
        return OS_EINVALPARAMS;
    }

    if (address > unit->PhysicalLimit || length - 1 > unit->PhysicalLimit - address) {
        WARNING("__ValidateExtent: extent %llx, length %llx exceeds the physical limit %llx at %llx",
                address, length, unit->PhysicalLimit, unit->Description->RegisterBase);
        return OS_EINVALPARAMS;
    }

    if (length > remaining) {
        WARNING("__ValidateExtent: extent length %llx exceeds the remaining mapping length %llx",
                length, remaining);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

// Checks the mapping's device address range, transfer direction, and physical
// extents before any table pages are reserved.
static oserr_t
__ValidateMapping(
    _In_ const ArmSMMUStage2Table_t*    table,
    _In_ const DMAIOMMUMappingConfig_t* config)
{
    const ArmSMMUStage2Unit_t* unit = table->Unit;
    uint64_t                   total = 0;
    int                        index;
    oserr_t                    status;

    if (config == NULL || config->Extents == NULL || config->ExtentCount <= 0 || config->Length == 0) {
        WARNING("__ValidateMapping: mapping has no extents or no length");
        return OS_EINVALPARAMS;
    }

    if ((config->DeviceAddress % ARM_SMMU_S2_PAGE_SIZE) != 0 || (config->Length % ARM_SMMU_S2_PAGE_SIZE) != 0) {
        WARNING("__ValidateMapping: device address %llx or length %llx is not page aligned",
                config->DeviceAddress, config->Length);
        return OS_EINVALPARAMS;
    }

    // Subtracting DeviceAddress from AddressLimit checks the end byte without
    // overflowing 64-bit arithmetic.
    if (config->DeviceAddress > unit->AddressLimit ||
        config->Length - 1 > unit->AddressLimit - config->DeviceAddress) {
        WARNING("__ValidateMapping: device range %llx, length %llx exceeds the address limit %llx at %llx",
                config->DeviceAddress, config->Length, unit->AddressLimit, unit->Description->RegisterBase);
        return OS_EINVALPARAMS;
    }

    if (config->Direction != DMAToDevice && config->Direction != DMAFromDevice &&
        config->Direction != DMABidirectional) {
        WARNING("__ValidateMapping: invalid direction %u", (uint32_t)config->Direction);
        return OS_EINVALPARAMS;
    }

    // Sum every extent to ensure the physical chunks match Length exactly.
    for (index = 0; index < config->ExtentCount; index++) {
        status = __ValidateExtent(table, &config->Extents[index], config->Length - total);
        if (status != OS_EOK) {
            return status;
        }
        total += config->Extents[index].Length;
    }
    
    if (total != config->Length) {
        WARNING("__ValidateMapping: extents cover %llx bytes, mapping length is %llx", total, config->Length);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

// Counts the maximum number of directory pages this mapping might need if none
// of its intermediate tables exist yet. At each level above the final 4 KiB
// page level, (last >> shift) - (start >> shift) + 1 gives the number of
// distinct table slots the range crosses, so each slot may need one child page.
static uint64_t
__CountTablePages(
    _In_ const ArmSMMUStage2Unit_t*     unit,
    _In_ const DMAIOMMUMappingConfig_t* config)
{
    uint64_t last = config->DeviceAddress + config->Length - 1;
    uint64_t count = 0;
    uint32_t level;
    uint32_t shift;

    for (level = unit->RootLevel; level < ARM_SMMU_S2_LEVEL_COUNT - 1U; level++) {
        shift = ARM_SMMU_S2_LEVEL0_SHIFT - (level * ARM_SMMU_S2_LEVEL_BITS);
        count += (last >> shift) - (config->DeviceAddress >> shift) + 1;
    }
    return count;
}

// Pre-allocates count zeroed 4 KiB table pages and appends them to
// mapping->SparePages. If an allocation fails partway through, already
// allocated pages stay on SparePages so the caller can release them.
static oserr_t
__ReservePages(
    _In_ const ArmSMMUStage2Table_t* table,
    _In_ ArmSMMUStage2Mapping_t*     mapping,
    _In_ uint64_t                    count)
{
    const ArmSMMUStage2Unit_t* unit = table->Unit;
    ArmSMMUStage2Page_t*       page;
    oserr_t                    status;

    while (count-- != 0) {
        page = kmalloc(sizeof(ArmSMMUStage2Page_t));
        if (page == NULL) {
            return OS_EOOM;
        }

        memset(page, 0, sizeof(ArmSMMUStage2Page_t));
        ELEMENT_INIT(&page->Link, 0, page);

        status = ArmSMMUMemoryAllocate(
            unit->Memory,
            ARM_SMMU_S2_PAGE_SIZE,
            ARM_SMMU_S2_PAGE_SIZE,
            &page->Block
        );
        if (status != OS_EOK) {
            if (unit->Memory->State == ArmSMMUMemoryFailed) {
                ERROR("__ReservePages: table page allocation at %llx failed: %u",
                      unit->Description->RegisterBase, status);
                __FailTable(table, status);
            }
            kfree(page);
            return status;
        }

        if (list_append(&mapping->SparePages, &page->Link) != 0) {
            // The new page could not be tracked on SparePages, so leave its
            // control block allocated and fail the unit to prevent reuse.
            ERROR("__ReservePages: page insertion at %llx failed", unit->Description->RegisterBase);
            return __FailTable(table, OS_EUNKNOWN);
        }
    }
    return OS_EOK;
}

oserr_t
ArmSMMUStage2MappingReserve(
    _In_ ArmSMMUStage2Table_t*          table,
    _In_ const DMAIOMMUMappingConfig_t* config,
    _In_ ArmSMMUStage2Mapping_t*        mapping)
{
    const ArmSMMUStage2Unit_t* unit = table->Unit;
    uint64_t                   count;
    oserr_t                    status;
    oserr_t                    cleanupStatus;

    mapping->Config = config;
    mapping->InstalledBytes = 0;
    list_construct(&mapping->SparePages);
    
    status = __ValidateMapping(table, config);
    if (status != OS_EOK) {
        return status;
    }

    if (table->MappingCount == UINT32_MAX) {
        WARNING("ArmSMMUStage2MappingReserve: table at %llx has too many mappings",
                unit->Description->RegisterBase);
        return OS_EOVERFLOW;
    }

    // Reject a request whose worst-case page count exceeds the entire control
    // pool up front, since another mapping may use up shared paths before
    // this mapping gets installed.
    count = __CountTablePages(unit, config);
    if (count > ARM_SMMU_MEMORY_MAX_SIZE / ARM_SMMU_S2_PAGE_SIZE) {
        WARNING("ArmSMMUStage2MappingReserve: mapping needs %llu table pages, more than the arena can hold",
                count);
        return OS_EOOM;
    }

    status = __ReservePages(table, mapping, count);
    if (status != OS_EOK) {
        cleanupStatus = __FreePages(table, &mapping->SparePages);
        if (cleanupStatus != OS_EOK) {
            // Leave SparePages intact while the unit failure prevents reuse.
            return cleanupStatus;
        }
        return status;
    }

    table->MappingCount++;
    return OS_EOK;
}

oserr_t
ArmSMMUStage2MappingRelease(
    _In_ ArmSMMUStage2Table_t*   table,
    _In_ ArmSMMUStage2Mapping_t* mapping)
{
    oserr_t status;

    if (table->MappingCount == 0) {
        ERROR("ArmSMMUStage2MappingRelease: no mapping is reserved at %llx",
              table->Unit->Description->RegisterBase);
        return __FailTable(table, OS_EUNKNOWN);
    }
    
    status = __FreePages(table, &mapping->SparePages);
    if (status != OS_EOK) {
        return status;
    }

    table->MappingCount--;
    return OS_EOK;
}

// Moves one pre-allocated table page from mapping->SparePages to table->Pages
// and links its physical address into an empty directory entry. Once linked,
// the page stays on table->Pages until ArmSMMUStage2TableDestroy runs.
static oserr_t
__LinkSparePage(
    _In_ ArmSMMUStage2Table_t*   table,
    _In_ ArmSMMUStage2Mapping_t* mapping,
    _In_ volatile uint64_t*      entry)
{
    const ArmSMMUStage2Unit_t* unit = table->Unit;
    ArmSMMUStage2Page_t*       page;

    if (mapping->SparePages.head == NULL) {
        ERROR("__LinkSparePage: no spare pages left at %llx", unit->Description->RegisterBase);
        return __FailTable(table, OS_EUNKNOWN);
    }

    page = mapping->SparePages.head->value;
    if (list_remove(&mapping->SparePages, &page->Link) != 0) {
        ERROR("__LinkSparePage: unlinking spare page at %llx failed", unit->Description->RegisterBase);
        return __FailTable(table, OS_EUNKNOWN);
    }

    if (list_append(&table->Pages, &page->Link) != 0) {
        ERROR("__LinkSparePage: inserting table page at %llx failed", unit->Description->RegisterBase);
        return __FailTable(table, OS_EUNKNOWN);
    }

    // Flush the zeroed page out to RAM before pointing the parent entry at it,
    // so the SMMU never sees uninitialized garbage as valid child entries.
    unit->Barrier();
    *entry = page->Block.PhysicalAddress | ARM_SMMU_S2_TABLE;
    return OS_EOK;
}

// Decodes a directory entry into the CPU virtual address of the next-level
// table, verifying the entry is a table descriptor inside the unit's pool.
static oserr_t
__FollowTableEntry(
    _In_  const ArmSMMUStage2Table_t* table,
    _In_  uint64_t                    entry,
    _Out_ volatile uint64_t**         tableOut)
{
    const ArmSMMUStage2Unit_t* unit = table->Unit;
    const ArmSMMUMemory_t*     memory = unit->Memory;
    uint64_t                   physical;
    uint64_t                   offset;

    if ((entry & ARM_SMMU_S2_TYPE_MASK) != ARM_SMMU_S2_TABLE) {
        ERROR("__FollowTableEntry: entry %llx at %llx is not a table", entry, unit->Description->RegisterBase);
        return __FailTable(table, OS_EUNKNOWN);
    }

    physical = entry & ARM_SMMU_S2_ADDRESS_MASK;
    if (physical < memory->Allocation.PhysicalBase) {
        ERROR("__FollowTableEntry: table %llx is below the arena at %llx", physical, unit->Description->RegisterBase);
        return __FailTable(table, OS_EUNKNOWN);
    }

    offset = physical - memory->Allocation.PhysicalBase;
    if (offset > memory->Allocation.Length - ARM_SMMU_S2_PAGE_SIZE) {
        ERROR("__FollowTableEntry: table %llx is above the arena at %llx", physical, unit->Description->RegisterBase);
        return __FailTable(table, OS_EUNKNOWN);
    }

    *tableOut = (volatile uint64_t*)(memory->Mapping + offset);
    return OS_EOK;
}

// Walks from Root down to the level-3 slot for address. Set install to true
// during writes so missing directory tables are filled from SparePages, and
// false during clears so missing tables trigger a corruption error.
static oserr_t
__FindLeaf(
    _In_  ArmSMMUStage2Table_t*   table,
    _In_  ArmSMMUStage2Mapping_t* mapping,
    _In_  uint64_t                address,
    _In_  bool                    install,
    _Out_ volatile uint64_t**     leafOut)
{
    volatile uint64_t* current = table->Root.VirtualAddress;
    volatile uint64_t* entry;
    uint32_t           level;
    uint32_t           shift;
    oserr_t            status;

    for (level = table->Unit->RootLevel; level < ARM_SMMU_S2_LEVEL_COUNT - 1U; level++) {
        shift = ARM_SMMU_S2_LEVEL0_SHIFT - level * ARM_SMMU_S2_LEVEL_BITS;
        entry = &current[(address >> shift) & (ARM_SMMU_S2_TABLE_ENTRIES - 1U)];

        if (*entry == ARM_SMMU_S2_INVALID && install) {
            status = __LinkSparePage(table, mapping, entry);
            if (status != OS_EOK) {
                return status;
            }
        }
        
        status = __FollowTableEntry(table, *entry, &current);
        if (status != OS_EOK) {
            return status;
        }
    }

    *leafOut = &current[(address >> ARM_SMMU_S2_PAGE_SHIFT) & (ARM_SMMU_S2_TABLE_ENTRIES - 1U)];
    return OS_EOK;
}

// Builds the permission and cache attribute bits shared by every 4 KiB page
// entry in a mapping.
static uint64_t
__GetLeafAttributes(
    _In_ const DMAIOMMUMappingConfig_t* config)
{
    uint64_t attributes;

    attributes = ARM_SMMU_S2_PAGE | ARM_SMMU_S2_ACCESS_FLAG | ARM_SMMU_S2_EXECUTE_NEVER |
                 ARM_SMMU_S2_NORMAL_WRITE_BACK | ARM_SMMU_S2_OUTER_SHAREABLE;
    if (config->Direction != DMAFromDevice) {
        attributes |= ARM_SMMU_S2_READ;
    }
    if (config->Direction != DMAToDevice) {
        attributes |= ARM_SMMU_S2_WRITE;
    }
    return attributes;
}

oserr_t
ArmSMMUStage2WriteLeaves(
    _In_ ArmSMMUStage2Table_t*   table,
    _In_ ArmSMMUStage2Mapping_t* mapping,
    _In_ ArmSMMUStage2Poll_t     poll,
    _In_ void*                   pollContext)
{
    const DMAIOMMUMappingConfig_t* config = mapping->Config;
    volatile uint64_t*             leaf;
    uint64_t                       attributes = __GetLeafAttributes(config);
    uint64_t                       deviceAddress;
    uint64_t                       extentOffset;
    int                            index;
    oserr_t                        status;

    // Walk every 4 KiB page of every extent in order, bumping InstalledBytes
    // after each write so a mid-loop stop leaves a single contiguous prefix.
    for (index = 0; index < config->ExtentCount; index++) {
        for (extentOffset = 0; extentOffset < config->Extents[index].Length; extentOffset += ARM_SMMU_S2_PAGE_SIZE) {
            status = poll(pollContext);
            if (status != OS_EOK) {
                return status;
            }

            deviceAddress = config->DeviceAddress + mapping->InstalledBytes;
            status = __FindLeaf(table, mapping, deviceAddress, true, &leaf);
            if (status != OS_EOK) {
                return status;
            }

            if (*leaf != ARM_SMMU_S2_INVALID) {
                ERROR("ArmSMMUStage2WriteLeaves: device address %llx is already mapped at %llx",
                      deviceAddress, table->Unit->Description->RegisterBase);
                return OS_EEXISTS;
            }

            *leaf = (config->Extents[index].Address + extentOffset) | attributes;
            mapping->InstalledBytes += ARM_SMMU_S2_PAGE_SIZE;
        }
    }
    return OS_EOK;
}

oserr_t
ArmSMMUStage2ClearLeaves(
    _In_ ArmSMMUStage2Table_t*   table,
    _In_ ArmSMMUStage2Mapping_t* mapping,
    _In_ ArmSMMUStage2Poll_t     poll,
    _In_ void*                   pollContext)
{
    volatile uint64_t* leaf;
    uint64_t           deviceAddress;
    oserr_t            status;

    // Clear from the end of the written range down to zero so an interrupted
    // unmap leaves a contiguous prefix that a later retry can finish cleanly.
    while (mapping->InstalledBytes != 0) {
        status = poll(pollContext);
        if (status != OS_EOK) {
            return status;
        }
        
        deviceAddress = mapping->Config->DeviceAddress + mapping->InstalledBytes - ARM_SMMU_S2_PAGE_SIZE;
        status = __FindLeaf(table, mapping, deviceAddress, false, &leaf);
        if (status != OS_EOK) {
            return status;
        }
        *leaf = ARM_SMMU_S2_INVALID;
        mapping->InstalledBytes -= ARM_SMMU_S2_PAGE_SIZE;
    }
    return OS_EOK;
}
