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
 * SMMUv3 stage-2 address spaces and DMA mappings.
 * Each space represents one isolated DMA address translation context tagged by
 * a hardware Virtual Machine ID (VMID). The shared stage-2 table code handles
 * allocating and writing the 4 KiB page-table tree, while this file assigns
 * VMIDs, tracks install/remove phases across timeouts, and queues VMID TLB
 * invalidations when mappings change.
 */

#include <debug.h>
#include <heap.h>
#include <string.h>

#include "private.h"

// Context passed to the shared page-table writer and clearer so each 4 KiB
// entry loop checks our deadline and unit health.
typedef struct __SMMUv3TablePoll {
    SMMUv3Unit_t*            Unit;
    const ArmSMMUDeadline_t* Deadline;
} __SMMUv3TablePoll_t;

// Scan the unit's VMID bitmap for a free identifier. VMID 0 is marked used at
// unit creation and is never handed out, so zeroed or unconfigured hardware
// entries cannot collide with an active DMA space.
static oserr_t
__FindFreeVMID(
    _In_  SMMUv3Unit_t* unit,
    _Out_ uint32_t*     vmidOut)
{
    uint32_t vmid;

    for (vmid = 1; vmid < unit->VMIDCount; vmid++) {
        if ((unit->VMIDs[vmid / 64U] & (1ULL << (vmid % 64U))) == 0) {
            *vmidOut = vmid;
            return OS_EOK;
        }
    }
    return OS_ENOTSUPPORTED;
}

static void
__Stage2Barrier(void)
{
    __SMMUv3Barrier();
}

void
SMMUv3Stage2Initialize(
    _In_ SMMUv3Unit_t* unit)
{
    unit->Stage2.Context = &unit->Common;
    unit->Stage2.Description = &unit->Common.Description;
    unit->Stage2.Memory = &unit->Common.Memory;
    unit->Stage2.Barrier = __Stage2Barrier;
    unit->Stage2.Fail = ArmSMMUUnitStage2Fail;
}

oserr_t
SMMUv3SpaceCreate(
    _In_    void*  unitContext,
    _InOut_ void** spaceOut)
{
    SMMUv3Unit_t*  unit = unitContext;
    SMMUv3Space_t* space;
    uint32_t       vmid;
    oserr_t        status;

    if (!ArmSMMUUnitIsReady(&unit->Common)) {
        WARNING("SMMUv3SpaceCreate: unit at %llx is not ready", unit->Common.Description.RegisterBase);
        return OS_EBUSY;
    }

    status = __FindFreeVMID(unit, &vmid);
    if (status != OS_EOK) {
        WARNING("SMMUv3SpaceCreate: all %u VMIDs are in use at %llx",
                unit->VMIDCount, unit->Common.Description.RegisterBase);
        return status;
    }

    space = kmalloc(sizeof(SMMUv3Space_t));
    if (space == NULL) {
        ERROR("SMMUv3SpaceCreate: allocating space at %llx failed", unit->Common.Description.RegisterBase);
        return OS_EOOM;
    }

    memset(space, 0, sizeof(SMMUv3Space_t));
    space->Unit = unit;
    space->VMID = vmid;
    ELEMENT_INIT(&space->Link, 0, space);

    status = ArmSMMUStage2TableCreate(&space->Table, &unit->Stage2);
    if (status != OS_EOK) {
        ERROR("SMMUv3SpaceCreate: creating page table for VMID %u at %llx failed: %u",
              vmid, unit->Common.Description.RegisterBase, status);
        kfree(space);
        return status;
    }

    if (list_append(&unit->Common.Spaces, &space->Link) != 0) {
        // List insertion cannot fail for these valid bounded records. If it
        // ever does, keep the root page table allocated so the pool stays sane.
        ERROR("SMMUv3SpaceCreate: space insertion at %llx failed", unit->Common.Description.RegisterBase);
        return ArmSMMUUnitFail(&unit->Common, OS_EUNKNOWN);
    }

    unit->VMIDs[vmid / 64U] |= 1ULL << (vmid % 64U);
    *spaceOut = space;
    TRACE("SMMUv3SpaceCreate: created VMID %u at %llx", vmid, unit->Common.Description.RegisterBase);
    return OS_EOK;
}

void
SMMUv3SpaceDestroy(
    _In_ void* unitContext,
    _In_ void* spaceContext)
{
    SMMUv3Unit_t*  unit = unitContext;
    SMMUv3Space_t* space = spaceContext;
    oserr_t        status;

    // Require all device connections and mapping reservations to be gone first
    // so hardware is never left walking a freed page-table tree.
    if (space->HasConnection || space->Table.MappingCount != 0) {
        ERROR("SMMUv3SpaceDestroy: VMID %u at %llx is still in use, %u mappings, connection %u",
              space->VMID, unit->Common.Description.RegisterBase, space->Table.MappingCount,
              (uint32_t)space->HasConnection);
        ArmSMMUUnitFail(&unit->Common, OS_EBUSY);
        return;
    }

    status = ArmSMMUStage2TableDestroy(&space->Table);
    if (status != OS_EOK) {
        ERROR("SMMUv3SpaceDestroy: freeing page tables for VMID %u at %llx failed: %u",
              space->VMID, unit->Common.Description.RegisterBase, status);
        return;
    }

    if (list_remove(&unit->Common.Spaces, &space->Link) != 0) {
        ERROR("SMMUv3SpaceDestroy: unlinking VMID %u failed", space->VMID);
        ArmSMMUUnitFail(&unit->Common, OS_EUNKNOWN);
        return;
    }

    unit->VMIDs[space->VMID / 64U] &= ~(1ULL << (space->VMID % 64U));
    TRACE("SMMUv3SpaceDestroy: released VMID %u at %llx",
          space->VMID, unit->Common.Description.RegisterBase);
    kfree(space);
}

oserr_t
SMMUv3MappingPrepare(
    _In_    void*                          spaceContext,
    _In_    const DMAIOMMUMappingConfig_t* config,
    _InOut_ void**                         mappingOut)
{
    SMMUv3Space_t*   space = spaceContext;
    SMMUv3Mapping_t* mapping;
    oserr_t          status;

    mapping = kmalloc(sizeof(SMMUv3Mapping_t));
    if (mapping == NULL) {
        ERROR("SMMUv3MappingPrepare: allocating mapping for VMID %u at %llx failed",
              space->VMID, space->Unit->Common.Description.RegisterBase);
        return OS_EOOM;
    }
    
    memset(mapping, 0, sizeof(SMMUv3Mapping_t));
    
    // Pre-allocate all worst-case intermediate directory pages now so later
    // Install calls write entries directly without risking out-of-memory errors.
    status = ArmSMMUStage2MappingReserve(&space->Table, config, &mapping->Stage2);
    if (status != OS_EOK) {
        ERROR("SMMUv3MappingPrepare: reserving page tables for VMID %u at %llx failed: %u",
              space->VMID, space->Unit->Common.Description.RegisterBase, status);
        
        // If freeing leftover spare pages failed, keep mapping allocated so
        // those pool entries are not leaked before unit teardown.
        if (!ArmSMMUStage2MappingHoldsPages(&mapping->Stage2)) {
            kfree(mapping);
        }
        return status;
    }

    *mappingOut = mapping;
    return OS_EOK;
}

// Stop writing new entries if the timeout expires or the SMMU reports a fault.
static oserr_t
__PollTableWrite(
    _In_ void* context)
{
    const __SMMUv3TablePoll_t* poll = context;

    if (ArmSMMUDeadlineExpired(poll->Deadline)) {
        WARNING("__PollTableWrite: page-table write timed out at %llx",
                poll->Unit->Common.Description.RegisterBase);
        return OS_ETIMEOUT;
    }
    return SMMUv3FaultCheck(poll->Unit);
}

// Clearing entries ignores hardware faults so unmap cleanup can still proceed
// after a device fault occurs.
static oserr_t
__PollTableClear(
    _In_ void* context)
{
    const __SMMUv3TablePoll_t* poll = context;

    if (ArmSMMUDeadlineExpired(poll->Deadline)) {
        WARNING("__PollTableClear: page-table clear timed out at %llx",
                poll->Unit->Common.Description.RegisterBase);
        return OS_ETIMEOUT;
    }
    return OS_EOK;
}

oserr_t
SMMUv3MappingInstall(
    _In_ void*    spaceContext,
    _In_ void*    mappingContext,
    _In_ uint64_t timeoutNs)
{
    SMMUv3Space_t*      space = spaceContext;
    SMMUv3Mapping_t*    mapping = mappingContext;
    ArmSMMUDeadline_t   deadline;
    __SMMUv3TablePoll_t poll;
    oserr_t             status;

    if (mapping->Phase != SMMUv3MappingPrepared) {
        WARNING("SMMUv3MappingInstall: mapping for VMID %u at %llx cannot install from phase %u",
                space->VMID, space->Unit->Common.Description.RegisterBase, (uint32_t)mapping->Phase);
        return OS_EBUSY;
    }

    mapping->Phase = SMMUv3MappingInstalling;

    status = ArmSMMUDeadlineBegin(timeoutNs, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    poll.Unit = space->Unit;
    poll.Deadline = &deadline;

    status = ArmSMMUStage2WriteLeaves(
        &space->Table,
        &mapping->Stage2,
        __PollTableWrite,
        &poll
    );
    if (status != OS_EOK) {
        return status;
    }

    // Flush written page-table entries to RAM, then invalidate the VMID's TLB
    // so hardware drops any cached negative entries covering this range.
    __SMMUv3Barrier();
    SMMUv3CommandBatchPrepareTLBInvalidation(&mapping->Batch, space->VMID);

    status = SMMUv3CommandBatchRun(space->Unit, &mapping->Batch, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    mapping->Phase = SMMUv3MappingInstalled;
    TRACE("SMMUv3MappingInstall: VMID %u at %llx mapped %llx bytes at device address %llx",
          space->VMID, space->Unit->Common.Description.RegisterBase,
          mapping->Stage2.Config->Length, mapping->Stage2.Config->DeviceAddress);
    return OS_EOK;
}

// Clear all written leaf entries in RAM before preparing the final VMID TLB
// invalidation. On timeout, Phase stays in ClearLeaves so the next retry picks
// up from Stage2.InstalledBytes.
static oserr_t
__ClearMapping(
    _In_ SMMUv3Space_t*          space,
    _In_ SMMUv3Mapping_t*        mapping,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    __SMMUv3TablePoll_t poll;
    oserr_t             status;

    poll.Unit = space->Unit;
    poll.Deadline = deadline;

    status = ArmSMMUStage2ClearLeaves(&space->Table, &mapping->Stage2, __PollTableClear, &poll);
    if (status != OS_EOK) {
        return status;
    }
    
    __SMMUv3Barrier();
    SMMUv3CommandBatchPrepareTLBInvalidation(&mapping->Batch, space->VMID);
    mapping->Phase = SMMUv3MappingInvalidate;
    return OS_EOK;
}

oserr_t
SMMUv3MappingRemove(
    _In_ void*    spaceContext,
    _In_ void*    mappingContext,
    _In_ uint64_t timeoutNs)
{
    SMMUv3Space_t*    space = spaceContext;
    SMMUv3Mapping_t*  mapping = mappingContext;
    ArmSMMUDeadline_t deadline;
    oserr_t           status;

    if (mapping->Phase == SMMUv3MappingRemoved) {
        return OS_EOK;
    }

    status = ArmSMMUDeadlineBegin(timeoutNs, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    // Remove runs three resumable steps in order:
    // 1. Wait for any in-flight install TLB invalidation to retire.
    // 2. Clear the written leaf entries backward to zero.
    // 3. Flush the VMID's TLB so hardware stops using the cleared pages.
    if (mapping->Phase < SMMUv3MappingWaitInstall) {
        mapping->Phase = SMMUv3MappingWaitInstall;
    }

    if (mapping->Phase == SMMUv3MappingWaitInstall) {
        if (mapping->Batch.State == SMMUv3BatchSubmitted) {
            status = SMMUv3CommandBatchRun(space->Unit, &mapping->Batch, &deadline);
            if (status != OS_EOK) {
                return status;
            }
        }
        mapping->Phase = SMMUv3MappingClearLeaves;
    }

    if (mapping->Phase == SMMUv3MappingClearLeaves) {
        status = __ClearMapping(space, mapping, &deadline);
        if (status != OS_EOK) {
            return status;
        }
    }

    status = SMMUv3CommandBatchRun(space->Unit, &mapping->Batch, &deadline);
    if (status != OS_EOK) {
        return status;
    }

    mapping->Phase = SMMUv3MappingRemoved;
    TRACE("SMMUv3MappingRemove: VMID %u at %llx unmapped %llx bytes at device address %llx",
          space->VMID, space->Unit->Common.Description.RegisterBase,
          mapping->Stage2.Config->Length, mapping->Stage2.Config->DeviceAddress);
    return OS_EOK;
}

void
SMMUv3MappingDestroy(
    _In_ void* spaceContext,
    _In_ void* mappingContext)
{
    SMMUv3Space_t*   space = spaceContext;
    SMMUv3Mapping_t* mapping = mappingContext;
    oserr_t          status;

    // Only never-installed or fully removed mappings may release their pages.
    if (mapping->Phase != SMMUv3MappingPrepared && mapping->Phase != SMMUv3MappingRemoved) {
        ERROR("SMMUv3MappingDestroy: mapping for VMID %u is still installed, phase %u",
              space->VMID, (uint32_t)mapping->Phase);
        ArmSMMUUnitFail(&space->Unit->Common, OS_EBUSY);
        return;
    }
    
    status = ArmSMMUStage2MappingRelease(&space->Table, &mapping->Stage2);
    if (status != OS_EOK) {
        ERROR("SMMUv3MappingDestroy: freeing mapping for VMID %u at %llx failed: %u",
              space->VMID, space->Unit->Common.Description.RegisterBase, status);
        return;
    }
    kfree(mapping);
}
