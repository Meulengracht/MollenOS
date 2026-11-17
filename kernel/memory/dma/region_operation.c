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
 * Lightweight recovery-group gating for DMA region Begin and End calls.
 * Tracks active region operations without taking the full device-context
 * setup path.
 */

//#define __TRACE

#include "attachment_private.h"
#include <assert.h>
#include <debug.h>

// Hold group->Access.Lock. Checks if a region Begin or End call can enter now.
// Recovery cleanup sets Cleaning while tearing down or resetting mappings, so
// both starts and completions wait with OS_EBUSY. If the binding lost its DMA
// context (Phase != DMABindingContextLive), neither call can proceed safely.
// Otherwise, completing an already-running transfer stays allowed even after
// the manager or binding stops accepting new work.
static oserr_t
__CheckRegionOperationAllowedLocked(
    _In_ DMABinding_t* binding,
    _In_ bool          complete)
{
    DMARecoveryGroup_t* group = binding->Group;

    if (group->Access.Cleaning) {
        return OS_EBUSY;
    }
    if (binding->Context.Phase != DMABindingContextLive) {
        return OS_ECANCELLED;
    }
    if (complete) {
        return OS_EOK;
    }
    return DMABindingCheckLiveLocked(binding);
}

oserr_t
DMABindingBeginRegionOperation(
    _In_    DMABinding_t* binding,
    _In_    bool          complete,
    _InOut_ bool*         counted)
{
    DMARecoveryGroup_t* group = binding->Group;
    oserr_t             status;
    oserr_t             countStatus = OS_EOK;

    assert(!*counted);

    // Flush any pending manager failure events before checking admission, then
    // take group->Access.Lock to gate the call and increment the group's active
    // operation count in one step.
    status = DMARecoveryCheckFailures(binding->Manager);
    if (status != OS_EOK) {
        ERROR("DMABindingBeginRegionOperation: failure propagation for binding %u failed: %u", binding->ID, status);
    }
    SpinlockAcquireIrq(&group->Access.Lock);
    status = __CheckRegionOperationAllowedLocked(binding, complete);
    if (status == OS_EOK) {
        countStatus = DMARecoveryAddOperationLocked(&group->Access);
        if (countStatus == OS_EOK) {
            *counted = true;
        }
        status = countStatus;
    }
    SpinlockReleaseIrq(&group->Access.Lock);

    // Log after dropping the IRQ spinlock. Rejections are normal while a
    // binding is stopping, so only a counter failure is logged as an error.
    if (countStatus != OS_EOK) {
        ERROR("DMABindingBeginRegionOperation: cannot count operation for binding %u: %u", binding->ID, countStatus);
    } else if (status != OS_EOK) {
        TRACE("DMABindingBeginRegionOperation: binding %u rejected region %s: %u",
            binding->ID, complete ? "completion" : "start", status);
    }
    return status;
}

// Re-acquire group->Access.Lock after cache prep and verify the binding is
// still live before marking a region token active. Keeps the spinlock held on
// OS_EOK so the caller can flip token state before a stop slips in.
oserr_t
DMABindingLockRegionCommit(
    _In_ DMABinding_t* binding)
{
    oserr_t status;

    SpinlockAcquireIrq(&binding->Group->Access.Lock);
    status = DMABindingCheckLiveLocked(binding);
    if (status != OS_EOK) {
        SpinlockReleaseIrq(&binding->Group->Access.Lock);
    }
    return status;
}

// Release the spinlock acquired by a successful DMABindingLockRegionCommit.
void
DMABindingUnlockRegionCommit(
    _In_ DMABinding_t* binding)
{
    SpinlockReleaseIrq(&binding->Group->Access.Lock);
}
