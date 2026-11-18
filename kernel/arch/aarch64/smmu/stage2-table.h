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
 * Shared page-table allocation, mapping validation, and 4 KiB page writes used
 * by both SMMUv3 and MMU-500. Each backend supplies its own hardware state
 * transitions, barriers, and TLB invalidations around these helpers.
 */

#ifndef __ARM_SMMU_STAGE2_TABLE_H__
#define __ARM_SMMU_STAGE2_TABLE_H__

#include <ds/list.h>

#include "description.h"
#include "memory.h"
#include "stage2.h"
#include "../../../memory/dma/backend.h"

/**
 * @brief Hardware unit hooks and limits needed by the shared page-table code.
 *
 * Each backend embeds one of these inside its unit struct. Set Context,
 * Description, Memory, Barrier, and Fail when creating the unit, then fill
 * RootLevel, AddressLimit, and PhysicalLimit once hardware capabilities are
 * probed. Tables keep a pointer to this struct, so the unit must outlive all
 * of its page tables.
 *
 * RootLevel is the starting walk depth (0 for 48-bit addresses, 1 for 39-bit
 * addresses). AddressLimit is the highest device DMA address the unit can
 * translate, and PhysicalLimit is the highest physical RAM address it can
 * output (both inclusive).
 *
 * Barrier flushes earlier CPU writes to page-table memory so the SMMU sees
 * them. Fail records a permanent unit failure when page-table consistency
 * breaks, receiving Context and the error code with the unit mutex held.
 */
typedef struct ArmSMMUStage2Unit {
    void*                       Context;
    const ArmSMMUDescription_t* Description;
    ArmSMMUMemory_t*            Memory;
    uint32_t                    RootLevel;
    uint64_t                    AddressLimit;
    uint64_t                    PhysicalLimit;
    void                        (*Barrier)(void);
    void                        (*Fail)(void* context, oserr_t status);
} ArmSMMUStage2Unit_t;

/**
 * @brief Page table for one DMA address space.
 *
 * Root is the top-level 4 KiB table page. Pages tracks every lower-level
 * directory page linked beneath Root, which stay allocated until the whole
 * table is destroyed so concurrent unmaps never pull out shared paths.
 * MappingCount counts active mapping reservations on this table.
 */
typedef struct ArmSMMUStage2Table {
    const ArmSMMUStage2Unit_t* Unit;
    ArmSMMUMemoryBlock_t       Root;
    list_t                     Pages;
    uint32_t                   MappingCount;
} ArmSMMUStage2Table_t;

/**
 * @brief Per-mapping page-table state embedded inside a backend mapping record.
 *
 * SparePages holds pre-allocated directory pages set aside when the mapping is
 * reserved so later page writes never run out of memory. InstalledBytes tracks
 * how many bytes from the start of the mapping currently have page entries
 * written, allowing clean rollback or retry on partial operations.
 */
typedef struct ArmSMMUStage2Mapping {
    const DMAIOMMUMappingConfig_t* Config;
    list_t                         SparePages;
    uint64_t                       InstalledBytes;
} ArmSMMUStage2Mapping_t;

/**
 * @brief Callback invoked before writing or clearing each 4 KiB page entry.
 *
 * Returning OS_EOK lets the walk continue. Any other status halts the loop
 * immediately and is passed back to the caller. Backends use this hook to
 * check their wait timeout and stop early if the SMMU reports a fault.
 */
typedef oserr_t (*ArmSMMUStage2Poll_t)(void* context);

/**
 * @brief Allocates the top-level page table for a new DMA address space.
 *
 * Hold the unit mutex. Pass a zeroed table and a fully configured unit.
 * Leaves table empty if allocation fails.
 *
 * @param table Zeroed page-table record to initialize.
 * @param unit  Unit configuration and control memory pool.
 * @return OS_EOK on success, or the control-memory allocation error.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUStage2TableCreate(
    _In_ ArmSMMUStage2Table_t*      table,
    _In_ const ArmSMMUStage2Unit_t* unit);

/**
 * @brief Frees all directory pages and the root table back to the control pool.
 *
 * Hold the unit mutex. Make sure hardware is no longer walking this table and
 * all mapping reservations have already been released. On error, this marks
 * the unit failed and leaves remaining pages allocated, so the caller must
 * keep table intact.
 *
 * @param table Page table to destroy.
 * @return OS_EOK on success, or the failure status if returning pages failed.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUStage2TableDestroy(
    _In_ ArmSMMUStage2Table_t* table);

/**
 * @brief Validates a DMA mapping and pre-allocates every table page it might need.
 *
 * Hold the unit mutex and pass a zeroed mapping record. Keep config valid and
 * unchanged until ArmSMMUStage2MappingRelease returns. Checks alignment,
 * address limits, transfer direction, and scatter-gather extents up front,
 * then reserves enough 4 KiB directory pages for the worst case where no
 * intermediate tables exist yet. Later writes move pages from SparePages into
 * the table without allocating.
 *
 * On error, nothing is counted against MappingCount and SparePages is empty,
 * unless freeing partial pages failed. When that happens the unit is marked
 * failed and the leftover pages remain in SparePages, so check
 * ArmSMMUStage2MappingHoldsPages before freeing the enclosing record.
 *
 * @param table   Target DMA address space table.
 * @param config  DMA range, direction, and physical memory extents.
 * @param mapping Zeroed mapping state to populate.
 * @return OS_EOK on success, OS_EINVALPARAMS for invalid mapping geometry,
 *         OS_EOVERFLOW if MappingCount is saturated, OS_EOOM if the pool
 *         cannot supply the needed pages, or another pool error.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUStage2MappingReserve(
    _In_ ArmSMMUStage2Table_t*          table,
    _In_ const DMAIOMMUMappingConfig_t* config,
    _In_ ArmSMMUStage2Mapping_t*        mapping);

/**
 * @brief Returns unused spare pages from a mapping and drops its reservation.
 *
 * Hold the unit mutex. All mapped pages for this mapping must already be
 * cleared. On error, the unit is marked failed and remaining pages stay in
 * SparePages, so the caller must keep mapping allocated.
 *
 * @param table   Page table where the mapping was reserved.
 * @param mapping Mapping to release.
 * @return OS_EOK on success, or the failure status if returning pages failed.
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUStage2MappingRelease(
    _In_ ArmSMMUStage2Table_t*   table,
    _In_ ArmSMMUStage2Mapping_t* mapping);

/**
 * @brief Reports whether unlinked spare table pages remain in mapping.
 *
 * @param mapping Mapping record to inspect.
 * @return true if SparePages still holds at least one page, false if empty.
 */
static inline bool
ArmSMMUStage2MappingHoldsPages(
    _In_ const ArmSMMUStage2Mapping_t* mapping)
{
    return mapping->SparePages.head != NULL;
}

/**
 * @brief Writes level-3 page entries for a mapping in ascending device-address order.
 *
 * Hold the unit mutex. Allocates missing directory levels from SparePages.
 * Updates InstalledBytes after each 4 KiB page so ArmSMMUStage2ClearLeaves
 * can cleanly roll back any partial write. When this returns, the new page
 * entries are written in RAM, and the backend must still run its barrier and
 * TLB invalidation to make them active in hardware.
 *
 * @param table       Target page table.
 * @param mapping     Reserved mapping to install.
 * @param poll        Callback run before each page write to check timeouts/faults.
 * @param pollContext Opaque pointer passed to poll.
 * @return OS_EOK on success, OS_EEXISTS if a page is already mapped, any
 *         non-OK status from poll, or OS_EUNKNOWN on table corruption (which
 *         marks the unit failed).
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUStage2WriteLeaves(
    _In_ ArmSMMUStage2Table_t*   table,
    _In_ ArmSMMUStage2Mapping_t* mapping,
    _In_ ArmSMMUStage2Poll_t     poll,
    _In_ void*                   pollContext);

/**
 * @brief Clears written page entries from the end of InstalledBytes back to zero.
 *
 * Hold the unit mutex. Works backward one 4 KiB page at a time and shrinks
 * InstalledBytes after each clear, so an interrupted unmap can be retried and
 * pick up where it stopped. The backend must still invalidate the TLB after
 * clearing entries.
 *
 * @param table       Target page table.
 * @param mapping     Mapping whose written prefix should be cleared.
 * @param poll        Callback run before each page clear to check timeouts/faults.
 * @param pollContext Opaque pointer passed to poll.
 * @return OS_EOK when InstalledBytes reaches zero, any non-OK status from poll,
 *         or OS_EUNKNOWN on table corruption (which marks the unit failed).
 */
KERNELABI oserr_t KERNELAPI
ArmSMMUStage2ClearLeaves(
    _In_ ArmSMMUStage2Table_t*   table,
    _In_ ArmSMMUStage2Mapping_t* mapping,
    _In_ ArmSMMUStage2Poll_t     poll,
    _In_ void*                   pollContext);

#endif //!__ARM_SMMU_STAGE2_TABLE_H__
