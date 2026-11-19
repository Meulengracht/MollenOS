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
 * MMU-500 context bank programming and global TLB synchronization.
 * Every routine here runs with the generic unit mutex held and drains earlier
 * TLB maintenance before changing context registers or submitting new work.
 *
 * Global TLB sync tracking: MMU-500 exposes one global sync status bit
 * (TLBGSTATUS.ACTIVE), shared across the unit. To let timed-out callers retry
 * safely without losing track of their work, each submitted sync gets a
 * monotonic ticket number (SyncsSubmitted). When TLBGSTATUS goes idle,
 * SyncsCompleted catches up to SyncsSubmitted. A returning caller only needs
 * to check whether its saved ticket is at or below SyncsCompleted.
 */

#include "private.h"
#include <debug.h>
#include <limits.h>
#include <string.h>

void
SMMU500SyncPrepare(
    _Out_ SMMU500Sync_t*        sync,
    _In_  enum SMMU500SyncScope scope,
    _In_  uint32_t              vmid)
{
    memset(sync, 0, sizeof(*sync));
    sync->Scope = scope;
    sync->VMID = vmid;
}

// Record a fatal mismatch between software ticket counters and hardware sync
// state. Because a lost or double-counted sync leaves TLB coherency unknown,
// mark hardware completion unreliable so control pages stay allocated.
static oserr_t
__SyncFailure(
    _In_ SMMU500Unit_t* unit,
    _In_ const char*    function,
    _In_ const char*    reason)
{
    ERROR("__SyncFailure: %s reported %s at %llx, submitted %llu, completed %llu",
          function, reason, unit->Common.Description.RegisterBase, unit->SyncsSubmitted, unit->SyncsCompleted);
    ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
    return OS_ECANCELLED;
}

// Called once TLBGSTATUS shows no active global sync. Re-check fault registers
// because errors from the draining sync or in-flight walks can surface right as
// the hardware goes idle. Bank fault registers are skipped before startup
// writes any bank controls, because firmware may have left stale bank faults.
static oserr_t
__FinishSync(
    _In_ SMMU500Unit_t* unit)
{
    oserr_t status;

    if (unit->StartProgress == SMMU500NoWrites) {
        status = SMMU500FaultCheck(unit);
    } else {
        status = SMMU500FaultCheckAll(unit);
    }

    // A unit failed through the generic DMA layer can still finish cleanup,
    // so only abort here when completion itself became unreliable.
    if (status != OS_EOK && unit->Common.CompletionUnreliable) {
        return OS_ECANCELLED;
    }

    if (unit->SyncsCompleted != unit->SyncsSubmitted) {
        TRACE("__FinishSync: unit %llx completed ticket %llu",
              unit->Common.Description.RegisterBase, unit->SyncsSubmitted);
    }
    unit->SyncsCompleted = unit->SyncsSubmitted;
    return OS_EOK;
}

oserr_t
SMMU500SyncWait(
    _In_ SMMU500Unit_t*           unit,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    uint32_t active;
    oserr_t  status;

    // At most one sync can be in flight at any time under the unit mutex.
    if (unit->SyncsCompleted > unit->SyncsSubmitted || unit->SyncsSubmitted - unit->SyncsCompleted > 1U) {
        return __SyncFailure(unit, "SMMU500SyncWait", "invalid sync counters");
    }

    for (;;) {
        status = SMMU500FaultCheck(unit);
        if (status != OS_EOK) {
            return status;
        }

        active = __SMMU500Read32(
            unit->Common.Registers,
            SMMU500_TLBGSTATUS
        );
        if ((active & SMMU500_SYNC_ACTIVE) == 0) {
            // Order any later page-table or register teardown after the idle read.
            __SMMU500Barrier();
            return __FinishSync(unit);
        }

        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("SMMU500SyncWait: global synchronization timeout at %llx, ticket %llu",
                    unit->Common.Description.RegisterBase, unit->SyncsSubmitted);
            return OS_ETIMEOUT;
        }
    }
}

// Assign the next ticket and immediately issue the TLB invalidation followed by
// TLBGSYNC. Keep this sequence atomic inside the mutex: no timeout check or
// fallible call may sit between bumping SyncsSubmitted and writing both MMIO
// registers, or ticket numbers and hardware state would drift apart.
static void
__SubmitSync(
    _In_    SMMU500Unit_t* unit,
    _InOut_ SMMU500Sync_t* sync)
{
    // Ensure prior page-table or route writes are visible before invalidating.
    __SMMU500Barrier();
    sync->Ticket = ++unit->SyncsSubmitted;
    
    TRACE("__SubmitSync: ticket %llu, scope %u, VMID %u", sync->Ticket, (uint32_t)sync->Scope, sync->VMID);
    if (sync->Scope == SMMU500SyncVMID) {
        __SMMU500Write32(unit->Common.Registers, SMMU500_TLBIVMID, sync->VMID);
    } else {
        __SMMU500Write32(unit->Common.Registers, SMMU500_TLBIALLNSNH, 0);
    }
    __SMMU500Write32(unit->Common.Registers, SMMU500_TLBGSYNC, 0);
    __SMMU500Barrier();
}

oserr_t
SMMU500SyncRun(
    _In_    SMMU500Unit_t*           unit,
    _InOut_ SMMU500Sync_t*           sync,
    _In_    const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    if ((sync->Scope != SMMU500SyncVMID && sync->Scope != SMMU500SyncAll) ||
        (sync->Scope == SMMU500SyncVMID && (sync->VMID == 0 || sync->VMID > unit->ContextCount))) {
        WARNING("SMMU500SyncRun: invalid scope %u or VMID %u", (uint32_t)sync->Scope, sync->VMID);
        return OS_EINVALPARAMS;
    }

    if (sync->Ticket > unit->SyncsSubmitted) {
        return __SyncFailure(unit, "SMMU500SyncRun", "saved ticket was never submitted");
    }

    status = SMMU500FaultCheck(unit);
    if (status != OS_EOK) {
        return status;
    }

    // If a later caller already drained our saved ticket and moved on to newer
    // work, return immediately instead of waiting on or repeating that request.
    if (sync->Ticket != 0 && sync->Ticket <= unit->SyncsCompleted) {
        return OS_EOK;
    }

    // Wait for any earlier in-flight sync (either ours or another caller's).
    status = SMMU500SyncWait(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }

    // If this was a retry of a previously submitted ticket, draining above
    // finished it.
    if (sync->Ticket != 0) {
        return OS_EOK;
    }

    if (unit->SyncsSubmitted == UINT64_MAX) {
        return __SyncFailure(unit, "SMMU500SyncRun", "sync counter overflow");
    }

    if (ArmSMMUDeadlineExpired(deadline)) {
        WARNING("SMMU500SyncRun: unit %llx timed out before submitting scope %u, VMID %u",
                unit->Common.Description.RegisterBase, (uint32_t)sync->Scope, sync->VMID);
        return OS_ETIMEOUT;
    }

    __SubmitSync(unit, sync);
    // Hardware may finish the sync right away, so an immediate idle read on the
    // first poll of SMMU500SyncWait is valid and completes this ticket.
    return SMMU500SyncWait(unit, deadline);
}

// Build the stage-2 CB_TCR value for this unit:
// - Bits [18:16]: OutputEncoding (physical output address width from IDR2).
// - Bits [13:12]: Outer-shareable table walks.
// - Bits [7:6]:   SL0 starting walk level (2 - RootLevel: 2 for level 0, 1 for level 1).
// - Bits [5:0]:   T0SZ = 64 - InputBits (device input address width).
// - Bits [11:8]:  Match the CPU pool's cache policy (write-back when cached).
static uint32_t
__BuildTranslationControl(
    _In_ SMMU500Unit_t* unit)
{
    uint32_t tcr;

    tcr = (unit->OutputEncoding << 16) |
          SMMU500_VTCR_OUTER_SHARED |
          ((2U - unit->Stage2.RootLevel) << 6) | (64U - unit->InputBits);
    if (unit->Common.Memory.Allocation.Cached) {
        tcr |= SMMU500_VTCR_CACHE_WB;
    }
    return tcr;
}

// Write the 64-bit physical address of the root page table into CB_TTBR0 and
// read it back. If the hardware rejects or truncates the address, the bank
// could walk arbitrary RAM, so pin the unit immediately.
static oserr_t
__WriteRoot(
    _In_ SMMU500Space_t* space,
    _In_ uint32_t        bankOffset)
{
    SMMU500Unit_t* unit = space->Unit;
    uint64_t       observed;

    __SMMU500Write64(unit->Common.Registers, bankOffset + SMMU500_CB_TTBR0, space->Table.Root.PhysicalAddress);
    __SMMU500Barrier();

    observed = __SMMU500Read64(
        unit->Common.Registers,
        bankOffset + SMMU500_CB_TTBR0
    );
    if (observed != space->Table.Root.PhysicalAddress) {
        ERROR("__WriteRoot: bank %u at %llx rejected root %llx, read %llx", space->Bank,
              unit->Common.Description.RegisterBase, (uint64_t)space->Table.Root.PhysicalAddress, observed);
        ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
        return OS_ECANCELLED;
    }
    return OS_EOK;
}

// Configure a reserved context bank for 64-bit stage-2 translation and turn it
// on. Order matters: select AArch64 format (CBA2R) and stage-2/VMID attributes
// (CBAR) in GR1 first, then set translation geometry (CB_TCR) and root table
// (CB_TTBR0) in the bank, and set CB_SCTLR.ENABLE last so hardware never runs
// with half-written translation settings. Every register write is verified.
static oserr_t
__ProgramContext(
    _In_ SMMU500Space_t* space)
{
    SMMU500Unit_t* unit = space->Unit;
    uint32_t       page1 = __SMMU500GlobalPage1Offset(unit);
    uint32_t       bankOffset = __SMMU500ContextBankOffset(unit, space->Bank);
    oserr_t        status;

    status = SMMU500WriteChecked(
        unit,
        page1 + SMMU500_CBA2R(space->Bank),
        SMMU500_CBA2R_VA64,
        SMMU500_CBA2R_FIELDS
    );
    if (status != OS_EOK) {
        return status;
    }

    // TYPE bits [17:16] stay 0 (stage-2 bank), and the low 8 bits hold the nonzero VMID.
    status = SMMU500WriteChecked(
        unit,
        page1 + SMMU500_CBAR(space->Bank),
        space->Bank + 1U,
        SMMU500_CBAR_FIELDS
    );
    if (status != OS_EOK) {
        return status;
    }

    status = SMMU500WriteChecked(
        unit,
        bankOffset + SMMU500_CB_TCR,
        __BuildTranslationControl(unit),
        SMMU500_VTCR_FIELDS
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __WriteRoot(space, bankOffset);
    if (status != OS_EOK) {
        return status;
    }
    return SMMU500WriteChecked(
        unit,
        bankOffset + SMMU500_CB_SCTLR,
        SMMU500_SCTLR_FAULTS | SMMU500_SCTLR_ENABLE,
        SMMU500_SCTLR_FIELDS
    );
}

oserr_t
SMMU500ContextEnable(
    _In_ SMMU500Space_t*          space,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    SMMU500Unit_t* unit = space->Unit;
    oserr_t        status;

    if (space->ContextProgress == SMMU500ContextEnabled) {
        return OS_EOK;
    }

    // Drain any prior global sync and check for unit failures before touching
    // bank registers. Device faults stay latched for SMMU500UnitPoll.
    status = SMMU500SyncWait(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = SMMU500FaultCheckAll(unit);
    if (status != OS_EOK) {
        return status;
    }

    if (space->ContextProgress == SMMU500ContextUnused || space->ContextProgress == SMMU500ContextDisabled) {
        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("SMMU500ContextEnable: unit %llx timed out before programming bank %u",
                    unit->Common.Description.RegisterBase, space->Bank);
            return OS_ETIMEOUT;
        }

        TRACE("SMMU500ContextEnable: programming bank %u at %llx",
              space->Bank, unit->Common.Description.RegisterBase);
        space->ContextProgress = SMMU500ContextProgramming;
        
        status = __ProgramContext(space);
        if (status != OS_EOK) {
            return status;
        }

        // Flush any stale TLB entries tagged with this bank's VMID before
        // callers route device streams to it.
        SMMU500SyncPrepare(
            &space->ContextSync,
            SMMU500SyncVMID,
            __SMMU500SpaceVMID(space)
        );
        space->ContextProgress = SMMU500ContextEnableSync;
    }

    if (space->ContextProgress != SMMU500ContextEnableSync) {
        WARNING("SMMU500ContextEnable: bank %u at %llx is busy in phase %u",
                space->Bank, unit->Common.Description.RegisterBase, (uint32_t)space->ContextProgress);
        return OS_EBUSY;
    }
    
    status = SMMU500SyncRun(unit, &space->ContextSync, deadline);
    if (status != OS_EOK) {
        return status;
    }
    space->ContextProgress = SMMU500ContextEnabled;
    return OS_EOK;
}

// Check for unit failures before turning off a context bank. A unit failed
// through the generic DMA layer still allows cleanup once routes are faulted
// and drained, while unreliable completion aborts so a stalled request never
// loses its bank. Device faults stay latched for SMMU500UnitPoll.
static oserr_t
__PollBeforeDisable(
    _In_ SMMU500Unit_t* unit)
{
    oserr_t status = SMMU500FaultCheckAll(unit);

    if (status != OS_EOK) {
        WARNING("__PollBeforeDisable: cleanup on failed unit at %llx", unit->Common.Description.RegisterBase);
        if (unit->Common.CompletionUnreliable) {
            return OS_ECANCELLED;
        }
    }
    return OS_EOK;
}

oserr_t
SMMU500ContextDisable(
    _In_ SMMU500Space_t*          space,
    _In_ const ArmSMMUDeadline_t* deadline)
{
    SMMU500Unit_t* unit = space->Unit;
    uint32_t       bankOffset = __SMMU500ContextBankOffset(unit, space->Bank);
    oserr_t        status;

    if (space->ContextProgress == SMMU500ContextUnused || space->ContextProgress == SMMU500ContextDisabled) {
        return OS_EOK;
    }

    status = SMMU500SyncWait(unit, deadline);
    if (status != OS_EOK) {
        return status;
    }

    status = __PollBeforeDisable(unit);
    if (status != OS_EOK) {
        return status;
    }

    if (space->ContextProgress != SMMU500ContextDisableSync) {
        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("SMMU500ContextDisable: unit %llx timed out before disabling bank %u",
                    unit->Common.Description.RegisterBase, space->Bank);
            return OS_ETIMEOUT;
        }
        
        // Disable the bank before flushing its VMID from the TLB so no cached
        // translation can survive into a future reuse of this bank.
        TRACE("SMMU500ContextDisable: disabling bank %u at %llx",
              space->Bank, unit->Common.Description.RegisterBase);
        status = SMMU500WriteChecked(
            unit,
            bankOffset + SMMU500_CB_SCTLR,
            0,
            SMMU500_SCTLR_FIELDS
        );
        if (status != OS_EOK) {
            return status;
        }

        SMMU500SyncPrepare(&space->ContextSync, SMMU500SyncVMID, __SMMU500SpaceVMID(space));
        space->ContextProgress = SMMU500ContextDisableSync;
    }

    status = SMMU500SyncRun(unit, &space->ContextSync, deadline);
    if (status != OS_EOK) {
        return status;
    }
    
    space->ContextProgress = SMMU500ContextDisabled;
    return OS_EOK;
}
