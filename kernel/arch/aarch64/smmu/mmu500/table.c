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
 * MMU-500 stage-2 address spaces and DMA mappings.
 * The shared stage-2 page-table helpers allocate, write, and free the 4 KiB
 * page-table tree. This module ties each space to a hardware context bank,
 * tracks mapping phases, and runs VMID-scoped TLB invalidations around updates.
 */

#include "private.h"
#include <debug.h>
#include <heap.h>
#include <string.h>

// Context passed to page-table write and clear loops so each 4 KiB page step
// checks the caller's deadline and unit health.
typedef struct SMMU500TablePoll {
    SMMU500Unit_t*           Unit;
    const ArmSMMUDeadline_t* Deadline;
} SMMU500TablePoll_t;

// Record a permanent unit failure when space or mapping accounting breaks.
static oserr_t
__TableFailure(
    _In_ SMMU500Unit_t* unit,
    _In_ const char*    function,
    _In_ const char*    operation,
    _In_ oserr_t        status)
{
    ERROR("__TableFailure: %s failed during %s at %llx: %u",
          function, operation, unit->Common.Description.RegisterBase, status);
    ArmSMMUUnitFail(&unit->Common, status);
    return status;
}

static void
__Stage2Barrier(void)
{
    __SMMU500Barrier();
}

void
SMMU500Stage2Initialize(
    _In_ SMMU500Unit_t* unit)
{
    unit->Stage2.Context = &unit->Common;
    unit->Stage2.Description = &unit->Common.Description;
    unit->Stage2.Memory = &unit->Common.Memory;
    unit->Stage2.Barrier = __Stage2Barrier;
    unit->Stage2.Fail = ArmSMMUUnitStage2Fail;
}

// Return the lowest hardware context bank index that has no assigned space.
static oserr_t
__FindFreeContextBank(
    _In_  SMMU500Unit_t* unit,
    _Out_ uint32_t*      bankOut)
{
    for (uint32_t bank = 0; bank < unit->ContextCount; bank++) {
        if (unit->Contexts[bank].Space == NULL) {
            *bankOut = bank;
            return OS_EOK;
        }
    }
    return OS_ENOTSUPPORTED;
}

oserr_t
SMMU500SpaceCreate(
    _In_    void*  unitContext,
    _InOut_ void** spaceOut)
{
    SMMU500Unit_t*  unit = unitContext;
    SMMU500Space_t* space;
    uint32_t        bank;
    oserr_t         status;

    if (!ArmSMMUUnitIsReady(&unit->Common)) {
        WARNING("SMMU500SpaceCreate: unit %llx is not ready", unit->Common.Description.RegisterBase);
        return OS_EBUSY;
    }

    status = __FindFreeContextBank(unit, &bank);
    if (status != OS_EOK) {
        WARNING("SMMU500SpaceCreate: all %u context banks are in use at %llx",
                unit->ContextCount, unit->Common.Description.RegisterBase);
        return status;
    }

    space = kmalloc(sizeof(SMMU500Space_t));
    if (space == NULL) {
        ERROR("SMMU500SpaceCreate: allocating space for bank %u at %llx failed",
              bank, unit->Common.Description.RegisterBase);
        return OS_EOOM;
    }

    memset(space, 0, sizeof(SMMU500Space_t));
    space->Unit = unit;
    space->Bank = bank;
    ELEMENT_INIT(&space->Link, 0, space);
    
    // Allocate the top-level stage-2 root page before exposing the space.
    status = ArmSMMUStage2TableCreate(&space->Table, &unit->Stage2);
    if (status != OS_EOK) {
        kfree(space);
        return status;
    }
    
    if (list_append(&unit->Common.Spaces, &space->Link) != 0) {
        // List insertion on a valid element is expected to succeed. If it
        // fails, keep the root page and unit pinned rather than leaking state.
        return __TableFailure(unit, "SMMU500SpaceCreate", "space insertion", OS_EUNKNOWN);
    }
    
    unit->Contexts[bank].Space = space;
    *spaceOut = space;
    TRACE("SMMU500SpaceCreate: bank %u at %llx", bank, unit->Common.Description.RegisterBase);
    return OS_EOK;
}

void
SMMU500SpaceDestroy(
    _In_ void* unitContext,
    _In_ void* spaceContext)
{
    SMMU500Unit_t*  unit = unitContext;
    SMMU500Space_t* space = spaceContext;
    oserr_t         status;

    // Require every mapping, stream connection, and active context bank to be
    // torn down before freeing shared page-table pages.
    if (unit->Common.CompletionUnreliable || space->HasConnection || space->Table.MappingCount != 0 ||
        (space->ContextProgress != SMMU500ContextUnused && space->ContextProgress != SMMU500ContextDisabled)) {
        ERROR("SMMU500SpaceDestroy: bank %u at %llx is still in use, %u mappings, connection %u, context phase %u",
              space->Bank, unit->Common.Description.RegisterBase, space->Table.MappingCount,
              (uint32_t)space->HasConnection, (uint32_t)space->ContextProgress);
        __TableFailure(unit, "SMMU500SpaceDestroy", "live space destruction", OS_EBUSY);
        return;
    }

    status = ArmSMMUStage2TableDestroy(&space->Table);
    if (status != OS_EOK) {
        return;
    }

    if (list_remove(&unit->Common.Spaces, &space->Link) != 0) {
        __TableFailure(unit, "SMMU500SpaceDestroy", "space unlink", OS_EUNKNOWN);
        return;
    }

    unit->Contexts[space->Bank].Space = NULL;
    TRACE("SMMU500SpaceDestroy: bank %u at %llx", space->Bank, unit->Common.Description.RegisterBase);
    kfree(space);
}

oserr_t
SMMU500MappingPrepare(
    _In_    void*                          spaceContext,
    _In_    const DMAIOMMUMappingConfig_t* config,
    _InOut_ void**                         mappingOut)
{
    SMMU500Space_t*   space = spaceContext;
    SMMU500Mapping_t* mapping;
    oserr_t           status;

    mapping = kmalloc(sizeof(*mapping));
    if (mapping == NULL) {
        ERROR("SMMU500MappingPrepare: allocating mapping for bank %u at %llx failed",
              space->Bank, space->Unit->Common.Description.RegisterBase);
        return OS_EOOM;
    }

    memset(mapping, 0, sizeof(*mapping));
    
    // Pre-allocate all directory pages this mapping might need so Install never
    // allocates memory.
    status = ArmSMMUStage2MappingReserve(
        &space->Table,
        config,
        &mapping->Stage2
    );
    if (status != OS_EOK) {
        // Keep the mapping record alive if spare pages could not be returned to
        // the control pool. That condition fails the unit and keeps RAM pinned.
        if (!ArmSMMUStage2MappingHoldsPages(&mapping->Stage2)) {
            kfree(mapping);
        }
        return status;
    }

    *mappingOut = mapping;
    TRACE("SMMU500MappingPrepare: bank %u, device address %llx, length %llx, %u spare pages",
          space->Bank, config->DeviceAddress, config->Length,
          (uint32_t)mapping->Stage2.SparePages.count);
    return OS_EOK;
}

// Per-page poll used while installing entries: check timeout and stop early if
// the unit encountered a fault.
static oserr_t
__PollWrite(
    _In_ void* context)
{
    const SMMU500TablePoll_t* poll = context;

    if (ArmSMMUDeadlineExpired(poll->Deadline)) {
        WARNING("__PollWrite: page-table write timed out at %llx", poll->Unit->Common.Description.RegisterBase);
        return OS_ETIMEOUT;
    }
    return SMMU500FaultCheck(poll->Unit);
}

// Per-page poll used while removing entries: check timeout only, ignoring unit
// faults so callers can still tear down mappings after a client fault.
static oserr_t
__PollClear(
    _In_ void* context)
{
    const SMMU500TablePoll_t* poll = context;

    if (ArmSMMUDeadlineExpired(poll->Deadline)) {
        WARNING("__PollClear: page-table clear timed out at %llx", poll->Unit->Common.Description.RegisterBase);
        return OS_ETIMEOUT;
    }
    return OS_EOK;
}

oserr_t
SMMU500MappingInstall(
    _In_ void*    spaceContext,
    _In_ void*    mappingContext,
    _In_ uint64_t timeoutNs)
{
    SMMU500Space_t*    space = spaceContext;
    SMMU500Mapping_t*  mapping = mappingContext;
    ArmSMMUDeadline_t  deadline;
    SMMU500TablePoll_t poll;
    oserr_t            status;

    if (mapping->Phase != SMMU500MappingPrepared) {
        WARNING("SMMU500MappingInstall: mapping of bank %u is in phase %u",
                space->Bank, (uint32_t)mapping->Phase);
        return OS_EBUSY;
    }

    mapping->Phase = SMMU500MappingInstalling;

    status = ArmSMMUDeadlineBegin(timeoutNs, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    // Wait for any earlier global sync before modifying page tables.
    status = SMMU500SyncWait(space->Unit, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    poll.Unit = space->Unit;
    poll.Deadline = &deadline;

    status = ArmSMMUStage2WriteLeaves(
        &space->Table,
        &mapping->Stage2,
        __PollWrite,
        &poll
    );
    if (status != OS_EOK) {
        return status;
    }

    // Flush CPU writes and invalidate stale TLB entries for this space's VMID
    // before reporting the mapping live.
    __SMMU500Barrier();
    SMMU500SyncPrepare(&mapping->Sync, SMMU500SyncVMID, __SMMU500SpaceVMID(space));
    
    status = SMMU500SyncRun(space->Unit, &mapping->Sync, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    mapping->Phase = SMMU500MappingInstalled;
    TRACE("SMMU500MappingInstall: bank %u, device address %llx, %llu bytes",
          space->Bank, mapping->Stage2.Config->DeviceAddress, mapping->Stage2.InstalledBytes);
    return OS_EOK;
}

// Clear all written page-table leaf entries and prepare the VMID TLB
// invalidation. If clearing times out, InstalledBytes stays updated so the
// next SMMU500MappingRemove call picks up from the remaining pages.
static oserr_t
__ClearMapping(
    _In_ SMMU500Space_t*          space,
    _In_ SMMU500Mapping_t*        mapping,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    SMMU500TablePoll_t poll;
    oserr_t            status;

    poll.Unit = space->Unit;
    poll.Deadline = deadline;

    status = ArmSMMUStage2ClearLeaves(&space->Table, &mapping->Stage2, __PollClear, &poll);
    if (status != OS_EOK) {
        return status;
    }

    __SMMU500Barrier();
    SMMU500SyncPrepare(
        &mapping->Sync,
        SMMU500SyncVMID,
        __SMMU500SpaceVMID(space)
    );
    return OS_EOK;
}

oserr_t
SMMU500MappingRemove(
    _In_ void*    spaceContext,
    _In_ void*    mappingContext,
    _In_ uint64_t timeoutNs)
{
    SMMU500Space_t*    space = spaceContext;
    SMMU500Mapping_t*  mapping = mappingContext;
    ArmSMMUDeadline_t  deadline;
    oserr_t            status;

    if (mapping->Phase == SMMU500MappingRemoved) {
        return OS_EOK;
    }

    status = ArmSMMUDeadlineBegin(timeoutNs, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = SMMU500SyncWait(space->Unit, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    // Sequence removal in three resumable stages: finish any install TLB sync
    // that timed out earlier, clear all leaf entries, then wait for a VMID TLB
    // sync so hardware cannot read stale translations after pages are freed.
    if (mapping->Phase < SMMU500MappingWaitInstall) {
        mapping->Phase = SMMU500MappingWaitInstall;
    }

    if (mapping->Phase == SMMU500MappingWaitInstall) {
        if (mapping->Sync.Ticket != 0) {
            status = SMMU500SyncRun(
                space->Unit,
                &mapping->Sync,
                &deadline
            );
            if (status != OS_EOK) {
                return status;
            }
        }
        mapping->Phase = SMMU500MappingClearLeaves;
    }

    if (mapping->Phase == SMMU500MappingClearLeaves) {
        status = __ClearMapping(space, mapping, &deadline);
        if (status != OS_EOK) {
            return status;
        }
        mapping->Phase = SMMU500MappingInvalidate;
    }

    status = SMMU500SyncRun(space->Unit, &mapping->Sync, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    mapping->Phase = SMMU500MappingRemoved;
    TRACE("SMMU500MappingRemove: bank %u, device address %llx", space->Bank, mapping->Stage2.Config->DeviceAddress);
    return OS_EOK;
}

void
SMMU500MappingDestroy(
    _In_ void* spaceContext,
    _In_ void* mappingContext)
{
    SMMU500Space_t*   space = spaceContext;
    SMMU500Mapping_t* mapping = mappingContext;
    oserr_t           status;

    if (space->Unit->Common.CompletionUnreliable ||
        (mapping->Phase != SMMU500MappingPrepared && mapping->Phase != SMMU500MappingRemoved)) {
        ERROR("SMMU500MappingDestroy: mapping of bank %u at %llx is still installed, phase %u",
              space->Bank, space->Unit->Common.Description.RegisterBase, (uint32_t)mapping->Phase);
        __TableFailure(space->Unit, "SMMU500MappingDestroy", "live mapping destruction", OS_EBUSY);
        return;
    }

    status = ArmSMMUStage2MappingRelease(
        &space->Table,
        &mapping->Stage2
    );
    if (status != OS_EOK) {
        return;
    }
    kfree(mapping);
}
