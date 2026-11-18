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
 * SMMUv3 command queue.
 * Software writes 16-byte commands into a circular ring buffer in RAM and
 * advances CMDQ_PROD. Hardware consumes them in order and advances CMDQ_CONS.
 * Every batch ends with CMD_SYNC so software can tell when earlier cache and
 * TLB invalidations have finished taking effect in hardware.
 */

#include <debug.h>
#include <limits.h>
#include <string.h>

#include "private.h"

// Read CMDQ_CONS and fold any hardware progress into unit->CommandsCompleted.
// SMMUv3 queue pointers store a slot index in the low bits plus one wrap bit
// above it, so pointers wrap modulo 2 * capacity. Because at most capacity
// commands can sit in the ring at once, sampling before every submission yields
// an unambiguous forward delta even across multiple ring wraps.
static oserr_t
__UpdateConsumer(
    _In_ SMMUv3Unit_t* unit)
{
    uint32_t capacity = 1U << unit->CommandBits;
    uint32_t mask = (capacity * 2U) - 1U;
    uint32_t consumer;
    uint32_t advance;

    consumer = __SMMUv3Read32(unit->Common.Registers, SMMUV3_CMDQ_CONS);
    advance = ((consumer & mask) - ((uint32_t)unit->CommandsCompleted & mask)) & mask;
    
    // Upper bits of CMDQ_CONS hold ERR, which is only valid while GERROR.CMDQ_ERR
    // is active. SMMUv3FaultCheck checks GERROR separately, so stale ERR bits on
    // a healthy queue are ignored here.
    if (unit->CommandsCompleted > unit->CommandsSubmitted ||
        unit->CommandsSubmitted - unit->CommandsCompleted > capacity ||
        advance > unit->CommandsSubmitted - unit->CommandsCompleted) {
        ERROR("__UpdateConsumer: invalid command consumer at %llx: %x, submitted %llu, completed %llu",
              unit->Common.Description.RegisterBase, consumer, unit->CommandsSubmitted, unit->CommandsCompleted);
        ArmSMMUUnitFail(&unit->Common, OS_EDEVFAULT);
        return OS_ECANCELLED;
    }

    unit->CommandsCompleted += advance;
    return OS_EOK;
}

// Verify that the command ring exists and the batch holds 1 to 3 commands
// ending in CMD_SYNC. Watching CMD_SYNC retire is how we know earlier commands
// in the batch have completed in hardware.
static oserr_t
__ValidateBatch(
    _In_ SMMUv3Unit_t*               unit,
    _In_ const SMMUv3CommandBatch_t* batch)
{
    if (unit->Commands.VirtualAddress == NULL) {
        ERROR("__ValidateBatch: command ring at %llx is not allocated", unit->Common.Description.RegisterBase);
        return OS_EINVALPARAMS;
    }

    if (batch->Count == 0 || batch->Count > 3 ||
        (batch->Commands[batch->Count - 1].Words[0] & SMMUV3_CMD_OPCODE_MASK) != SMMUV3_CMD_SYNC) {
        ERROR("__ValidateBatch: invalid command batch at %llx, count %u",
              unit->Common.Description.RegisterBase, batch->Count);
        return OS_EINVALPARAMS;
    }
    return OS_EOK;
}

// Return true if the ring has at least count free slots before wrapping into
// unconsumed entries.
static bool
__HasRingSpace(
    _In_ SMMUv3Unit_t* unit,
    _In_ uint32_t      count)
{
    uint32_t capacity = 1U << unit->CommandBits;
    return capacity - (unit->CommandsSubmitted - unit->CommandsCompleted) >= count;
}

// Write the batch entries into the command ring and notify hardware by updating
// CMDQ_PROD. A barrier forces the command bytes to RAM before CMDQ_PROD moves.
// Marking the batch submitted first prevents any retry from queuing it twice.
static oserr_t
__SubmitBatch(
    _In_    SMMUv3Unit_t*         unit,
    _InOut_ SMMUv3CommandBatch_t* batch)
{
    SMMUv3Command_t* ring = unit->Commands.VirtualAddress;
    uint32_t         capacity = 1U << unit->CommandBits;

    if (unit->CommandsSubmitted > UINT64_MAX - batch->Count) {
        ERROR("__SubmitBatch: command position overflow at %llx", unit->Common.Description.RegisterBase);
        return ArmSMMUUnitFail(&unit->Common, OS_EOVERFLOW);
    }

    for (uint32_t index = 0; index < batch->Count; index++) {
        ring[(unit->CommandsSubmitted + index) & (capacity - 1U)] = batch->Commands[index];
    }
    
    __SMMUv3Barrier();
    unit->CommandsSubmitted += batch->Count;
    batch->EndPosition = unit->CommandsSubmitted;
    batch->State = SMMUv3BatchSubmitted;
    __SMMUv3Write32(unit->Common.Registers, SMMUV3_CMDQ_PROD,
                   (uint32_t)unit->CommandsSubmitted & ((capacity * 2U) - 1U));
    return OS_EOK;
}

// Prepare a two-command batch that drops all cached TLB translations for vmid
// and waits via CMD_SYNC until hardware finishes flushing them.
void
SMMUv3CommandBatchPrepareTLBInvalidation(
    _Out_ SMMUv3CommandBatch_t* batch,
    _In_  uint32_t              vmid)
{
    memset(batch, 0, sizeof(*batch));
    batch->Count = 2;
    batch->Commands[0].Words[0] = SMMUV3_CMD_TLBI_VMID | ((uint64_t)vmid << SMMUV3_CMD_ID_SHIFT);
    batch->Commands[1].Words[0] = SMMUV3_CMD_SYNC;
}

oserr_t
SMMUv3CommandBatchRun(
    _In_    SMMUv3Unit_t*           unit,
    _InOut_ SMMUv3CommandBatch_t*   batch,
    _In_    const ArmSMMUDeadline_t* deadline)
{
    oserr_t status;

    status = __ValidateBatch(unit, batch);
    if (status != OS_EOK) {
        return status;
    }

    // Poll until our saved EndPosition retires or the deadline expires:
    // check global hardware errors, advance CommandsCompleted from CMDQ_CONS,
    // test for completion or timeout, and submit once if space is available.
    for (;;) {
        status = SMMUv3FaultCheck(unit);
        if (status != OS_EOK) {
            return status;
        }

        status = __UpdateConsumer(unit);
        if (status != OS_EOK) {
            return status;
        }

        if (batch->State != SMMUv3BatchPrepared && unit->CommandsCompleted >= batch->EndPosition) {
            // The trailing CMD_SYNC has finished. Checking earlier commands in
            // the batch is unsafe because their invalidations may still be running.
            __SMMUv3Barrier();
            batch->State = SMMUv3BatchComplete;
            return OS_EOK;
        }

        if (ArmSMMUDeadlineExpired(deadline)) {
            WARNING("SMMUv3CommandBatchRun: command timeout at %llx, batch state %u, end %llu, submitted %llu, completed %llu",
                    unit->Common.Description.RegisterBase, (uint32_t)batch->State,
                    batch->EndPosition, unit->CommandsSubmitted, unit->CommandsCompleted);
            return OS_ETIMEOUT;
        }
        
        if (batch->State == SMMUv3BatchPrepared && __HasRingSpace(unit, batch->Count)) {
            status = __SubmitBatch(unit, batch);
            if (status != OS_EOK) {
                return status;
            }
        }
    }
}
