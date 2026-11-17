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
 * Implements Begin, End, and process-exit Cancel for pre-allocated DMA
 * region token slots.
 */

//#define __TRACE

#include "private.h"
#include <shm/device_region.h>
#include <assert.h>
#include <debug.h>
#include <limits.h>

// Record this syscall on record->PendingOperation before counting it with the
// recovery group. If the calling process exits mid-call, SHMDeviceRegionCancel
// reads PendingOperation to locate the session and slot. Callers must always
// pair this with __FinishOperation (even on error), unless leaving
// PendingOperation attached for Cancel.
static oserr_t
__StartOperation(
    _In_ SHMRecord_t*              record,
    _In_ SHMDeviceRegionSession_t* session,
    _In_ unsigned int              slot,
    _In_ bool                      complete)
{
    oserr_t status;

    assert(record->PendingOperation == NULL && session->Record == NULL);
    SpinlockAcquireIrq(&session->Lock);
    session->OperationSlot = slot;
    session->Record = record;
    record->PendingOperation = session;
    SpinlockReleaseIrq(&session->Lock);
    status = DMABindingBeginRegionOperation(session->Mapping->Context->Binding, complete, &session->Counted);
    if (status != OS_EOK) {
        TRACE("__StartOperation: slot %u of session %p was not accepted: %u", slot, session, status);
    }
    return status;
}

// Drop the recovery group's operation count first, then detach PendingOperation
// from record so Cancel will no longer touch this slot.
static void
__FinishOperation(
    _In_ SHMDeviceRegionSession_t* session)
{
    DMABindingEndRegionOperation(session->Mapping->Context->Binding, &session->Counted);
    SpinlockAcquireIrq(&session->Lock);
    session->Record->PendingOperation = NULL;
    session->Record = NULL;
    SpinlockReleaseIrq(&session->Lock);
}

// Hold session->Lock. Roll back a failed Begin that never activated hardware,
// restoring the slot's earlier completed generation and phase so late End
// queries for the previous transfer keep working.
static oserr_t
__UndoPreparation(
    _In_ SHMDeviceRegionToken_t* token)
{
    oserr_t status;

    status = SHMRegionUsageEnd(&token->Access, token->Access.Generation);
    if (status != OS_EOK) {
        ERROR("__UndoPreparation: cannot release generation %u: %u", token->Access.Generation, status);
        return status;
    }
    token->Access.Generation = token->Generation;
    token->Phase = token->Generation == 0 ? SHMDeviceRegionIdle : SHMDeviceRegionFinished;
    return OS_EOK;
}

// Reserve the region in the buffer's usage tracker and move the slot into
// Preparing. Devices that only read (DMAToDevice) share with other readers,
// while writes (DMAFromDevice or DMABidirectional) require exclusive access.
static oserr_t
__PrepareToken(
    _In_ SHMDeviceRegionSession_t* session,
    _In_ unsigned int              slot,
    _In_ unsigned int              generation,
    _In_ enum DMADirection         direction)
{
    SHMDeviceRegionToken_t* token = &session->Tokens[slot];
    oserr_t                 status;

    SpinlockAcquireIrq(&session->Lock);
    if (token->Phase != SHMDeviceRegionIdle && token->Phase != SHMDeviceRegionFinished) {
        status = OS_EBUSY;
    } else {
        status = SHMRegionUsageBegin(session->Region->Binding.Usage, &token->Access,
            generation, direction != DMAToDevice);
        if (status == OS_EOK) {
            token->Direction = direction;
            token->Phase = SHMDeviceRegionPreparing;
        }
    }
    SpinlockReleaseIrq(&session->Lock);
    if (status != OS_EOK) {
        TRACE("__PrepareToken: slot %u generation %u of session %p was not prepared: %u",
            slot, generation, session, status);
    }
    return status;
}

// Check that slot, generation, and direction are valid for this session and do
// not exceed the direction allowed by the parent mapping.
static oserr_t
__CheckBeginRequest(
    _In_ SHMDeviceRegionSession_t* session,
    _In_ unsigned int              slot,
    _In_ unsigned int              generation,
    _In_ enum DMADirection         direction)
{
    SHMDeviceMapping_t* mapping = session->Mapping;

    if (slot >= session->Capacity || generation == 0 ||
        (direction != DMAToDevice && direction != DMAFromDevice && direction != DMABidirectional)) {
        WARNING("__CheckBeginRequest: slot %u, generation %u or direction %u is not valid for session %p",
            slot, generation, (unsigned int)direction, session);
        return OS_EINVALPARAMS;
    }
    if (mapping->Direction != DMABidirectional && mapping->Direction != direction) {
        WARNING("__CheckBeginRequest: direction %u is not allowed by mapping direction %u",
            (unsigned int)direction, (unsigned int)mapping->Direction);
        return OS_EPERMISSIONS;
    }
    return OS_EOK;
}

// Commit a prepared token to Active. Cache prep runs without Access.Lock held,
// so re-check that the binding stayed live under Access.Lock before activating.
// If the binding stopped in the meantime, roll back the reservation and put any
// rollback failure into *undoStatusOut.
static oserr_t
__ActivateToken(
    _In_  SHMDeviceRegionSession_t* session,
    _In_  SHMDeviceRegionToken_t*   token,
    _In_  unsigned int              generation,
    _Out_ oserr_t*                  undoStatusOut)
{
    SHMDeviceMapping_t* mapping = session->Mapping;
    oserr_t             status;

    *undoStatusOut = OS_EOK;
    status = DMABindingLockRegionCommit(mapping->Context->Binding);
    if (status == OS_EOK) {
        SpinlockAcquireIrq(&session->Lock);
        token->Generation = generation;
        token->Phase = SHMDeviceRegionActive;
        SpinlockReleaseIrq(&session->Lock);
        DMABindingUnlockRegionCommit(mapping->Context->Binding);
        return OS_EOK;
    }

    TRACE("__ActivateToken: generation %u of session %p was not committed: %u", generation, session, status);
    SpinlockAcquireIrq(&session->Lock);
    *undoStatusOut = __UndoPreparation(token);
    SpinlockReleaseIrq(&session->Lock);
    return status;
}

oserr_t
SHMDeviceRegionBegin(
    _In_ SHMRecord_t*              record,
    _In_ SHMDeviceRegionSession_t* session,
    _In_ unsigned int              slot,
    _In_ unsigned int              generation,
    _In_ enum DMADirection         direction)
{
    SHMDeviceMapping_t*     mapping = session->Mapping;
    SHMDeviceRegionToken_t* token;
    oserr_t                 status;
    oserr_t                 undoStatus;

    status = __CheckBeginRequest(session, slot, generation, direction);
    if (status != OS_EOK) {
        return status;
    }
    token = &session->Tokens[slot];
    status = __StartOperation(record, session, slot, false);
    if (status != OS_EOK) {
        goto exit;
    }
    if (mapping->State != SHMDeviceMappingReady) {
        TRACE("SHMDeviceRegionBegin: mapping %p is no longer ready", mapping);
        status = OS_ECANCELLED;
        goto exit;
    }
    status = __PrepareToken(session, slot, generation, direction);
    if (status != OS_EOK) {
        goto exit;
    }

    // The region reservation blocks conflicting CPU or device accesses while we
    // flush caches, and the group operation count blocks mapping teardown.
    SHMDeviceRegionsCache(mapping, session->Region, direction, false);
    status = __ActivateToken(session, token, generation, &undoStatus);
    if (undoStatus != OS_EOK) {
        // Keep PendingOperation attached so SHMDeviceRegionCancel can finish
        // undoing this slot if the process exits.
        ERROR("SHMDeviceRegionBegin: cannot undo slot %u generation %u of session %p: %u",
            slot, generation, session, undoStatus);
        return undoStatus;
    }
exit:
    __FinishOperation(session);
    return status;
}

// Invalidate CPU caches after hardware completes, then release the region
// reservation. Safe to re-run after an interrupted syscall because the region
// reservation remains held until SHMRegionUsageEnd succeeds.
static oserr_t
__FinishToken(
    _In_ SHMDeviceRegionSession_t* session,
    _In_ SHMDeviceRegionToken_t*   token)
{
    oserr_t status;

    SHMDeviceRegionsCache(session->Mapping, session->Region, token->Direction, true);
    SpinlockAcquireIrq(&session->Lock);
    status = SHMRegionUsageEnd(&token->Access, token->Generation);
    if (status == OS_EOK) {
        token->Result = OS_EOK;
        token->Phase = SHMDeviceRegionFinished;
    }
    SpinlockReleaseIrq(&session->Lock);
    if (status != OS_EOK) {
        ERROR("__FinishToken: cannot end generation %u: %u", token->Generation, status);
    }
    return status;
}

// Hold session->Lock. Return the status for a slot that is already done, still
// draining under recovery, or unknown for this generation.
static oserr_t
__GetEndResult(
    _In_ SHMDeviceRegionToken_t* token,
    _In_ unsigned int            generation)
{
    if (generation != token->Generation || token->Phase == SHMDeviceRegionIdle) {
        return OS_ENOENT;
    }
    if (token->Phase == SHMDeviceRegionFinished) {
        return token->Result;
    }
    return OS_EBUSY;
}

// Hold session->Lock. Returns true when this slot is live for generation.
static bool
__IsActiveGeneration(
    _In_ const SHMDeviceRegionToken_t* token,
    _In_ unsigned int                  generation)
{
    return token->Phase == SHMDeviceRegionActive && token->Generation == generation;
}

// Atomically move an active slot to Finishing under session->Lock so recovery
// sees it is already draining. If recovery already stopped it or it finished
// earlier, *finishingOut is false and the caller gets the stored status.
static oserr_t
__BeginFinish(
    _In_  SHMDeviceRegionSession_t* session,
    _In_  SHMDeviceRegionToken_t*   token,
    _In_  unsigned int              generation,
    _Out_ bool*                     finishingOut)
{
    oserr_t status = OS_EOK;

    SpinlockAcquireIrq(&session->Lock);
    *finishingOut = __IsActiveGeneration(token, generation);
    if (*finishingOut) {
        token->Phase = SHMDeviceRegionFinishing;
    } else {
        status = __GetEndResult(token, generation);
    }
    SpinlockReleaseIrq(&session->Lock);
    return status;
}

oserr_t
SHMDeviceRegionEnd(
    _In_ SHMRecord_t*              record,
    _In_ SHMDeviceRegionSession_t* session,
    _In_ unsigned int              slot,
    _In_ unsigned int              generation)
{
    SHMDeviceRegionToken_t* token;
    oserr_t                 status;
    bool                    active;
    bool                    finishing;

    if (slot >= session->Capacity || generation == 0) {
        WARNING("SHMDeviceRegionEnd: slot %u or generation %u is not valid for session %p",
            slot, generation, session);
        return OS_EINVALPARAMS;
    }
    token = &session->Tokens[slot];

    // Fast path: already finished transfers or unknown generations return
    // directly without touching the recovery group.
    SpinlockAcquireIrq(&session->Lock);
    active = __IsActiveGeneration(token, generation);
    status = __GetEndResult(token, generation);
    SpinlockReleaseIrq(&session->Lock);
    if (!active) {
        return status;
    }

    status = __StartOperation(record, session, slot, true);
    if (status != OS_EOK) {
        goto exit;
    }
    status = __BeginFinish(session, token, generation, &finishing);
    if (finishing) {
        status = __FinishToken(session, token);
        if (status != OS_EOK) {
            // Keep PendingOperation attached so SHMDeviceRegionCancel can retry
            // releasing the region reservation during teardown.
            return status;
        }
    }
exit:
    __FinishOperation(session);
    return status;
}

oserr_t
SHMDeviceRegionCancel(
    _In_ SHMRecord_t* record)
{
    SHMDeviceRegionSession_t* session = record->PendingOperation;
    SHMDeviceRegionToken_t*   token;
    enum SHMDeviceRegionPhase phase;
    oserr_t                   status = OS_EOK;

    if (session == NULL) {
        return OS_EOK;
    }

    // All caller threads have stopped. Revoke the device binding first so any
    // active hardware transfers are stopped and faulted, then undo an
    // interrupted Begin (Preparing) or complete an interrupted End (Finishing).
    DMABindingRevoke(session->Mapping->Context->Binding);
    token = &session->Tokens[session->OperationSlot];
    SpinlockAcquireIrq(&session->Lock);
    phase = token->Phase;
    if (phase == SHMDeviceRegionPreparing) {
        status = __UndoPreparation(token);
    }
    SpinlockReleaseIrq(&session->Lock);
    if (status != OS_EOK) {
        ERROR("SHMDeviceRegionCancel: cannot undo slot %u of session %p: %u", session->OperationSlot, session, status);
        return status;
    }
    if (phase == SHMDeviceRegionFinishing) {
        status = __FinishToken(session, token);
        if (status != OS_EOK) {
            ERROR("SHMDeviceRegionCancel: cannot finish slot %u of session %p: %u", session->OperationSlot, session, status);
            return status;
        }
    }
    TRACE("SHMDeviceRegionCancel: cancelled slot %u of session %p in phase %u",
        session->OperationSlot, session, (unsigned int)phase);
    __FinishOperation(session);
    return OS_EOK;
}
