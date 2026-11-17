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
 * Manages DMA region session lifetimes: creating sessions bound to one
 * region, unlinking drained sessions on close or process exit, and collecting
 * abandoned sessions after device recovery finishes.
 */

//#define __TRACE

#include "private.h"
#include <shm/device_region.h>
#include <assert.h>
#include <debug.h>
#include <heap.h>
#include <limits.h>
#include <string.h>

// Match the requested buffer ID, region index, and generation against the
// regions included in this mapping.
static SHMDeviceRegion_t*
__FindRegion(
    _In_ SHMDeviceMapping_t*        mapping,
    _In_ const OSBufferIdentity_t* identity)
{
    for (unsigned int i = 0; i < mapping->Regions->Count; i++) {
        SHMDeviceRegion_t* candidate = &mapping->Regions->Entries[i];

        if (candidate->Binding.Identity.Buffer == identity->Buffer &&
            candidate->Binding.Identity.Region == identity->Region &&
            candidate->Binding.Identity.Generation == identity->Generation) {
            return candidate;
        }
    }
    return NULL;
}

// Link the session onto the mapping's list and take a mapping reference so the
// mapping struct stays allocated until this session closes or drains. Fails if
// mapping cleanup has started or the mapping is no longer ready.
static oserr_t
__LinkSession(
    _In_ SHMDeviceMapping_t*       mapping,
    _In_ SHMDeviceRegionSession_t* session)
{
    oserr_t status;

    MutexLock(&mapping->Context->MappingLock);
    SpinlockAcquireIrq(&session->Lock);
    if (mapping->Lifetime.Cleaning || mapping->State != SHMDeviceMappingReady) {
        status = OS_EBUSY;
    } else if (mapping->Regions->SessionCount == UINT_MAX) {
        status = OS_EOVERFLOW;
    } else {
        status = SHMDeviceMappingAcquire(mapping);
        if (status == OS_EOK) {
            session->Mapping = mapping;
            session->Next = mapping->Regions->Sessions;
            mapping->Regions->Sessions = session;
            mapping->Regions->SessionCount++;
        }
    }
    MutexUnlock(&mapping->Context->MappingLock);
    SpinlockReleaseIrq(&session->Lock);
    return status;
}

oserr_t
SHMDeviceRegionSessionCreate(
    _In_  SHMDeviceMapping_t*         mapping,
    _In_  const OSBufferIdentity_t*  identity,
    _In_  unsigned int               capacity,
    _In_  SHMRecord_t*               record,
    _Out_ OSDmaRegionSession_t*      resultOut)
{
    SHMDeviceRegionSession_t* session;
    SHMDeviceRegion_t*        region;
    size_t                    size;
    oserr_t                   status;

    if (mapping->Kind != SHMDeviceMappingRegions || capacity == 0 ||
        capacity > SHM_REGION_TOKEN_LIMIT) {
        WARNING("SHMDeviceRegionSessionCreate: mapping kind %u or capacity %u is not valid",
            (unsigned int)mapping->Kind, capacity);
        return OS_EINVALPARAMS;
    }
    region = __FindRegion(mapping, identity);
    if (region == NULL) {
        WARNING("SHMDeviceRegionSessionCreate: buffer %u region %u generation %u is not in the mapping",
            identity->Buffer, identity->Region, identity->Generation);
        return OS_ENOENT;
    }
    size = sizeof(*session) + capacity * sizeof(session->Tokens[0]);
    session = kmalloc(size);
    if (session == NULL) {
        ERROR("SHMDeviceRegionSessionCreate: failed to allocate %u bytes for a session", (unsigned int)size);
        return OS_EOOM;
    }
    memset(session, 0, size);
    SpinlockConstruct(&session->Lock);
    session->Region = region;
    session->Capacity = capacity;

    // Assign record->Object before linking so SHMRecordCleanup frees this
    // session if linking or copying resultOut back to user space fails.
    record->Object = session;

    status = __LinkSession(mapping, session);
    if (status != OS_EOK) {
        WARNING("SHMDeviceRegionSessionCreate: cannot link a session to mapping %p: %u", mapping, status);
        return status;
    }

    resultOut->Identity = region->Binding.Identity;
    resultOut->Offset = region->Offset;
    resultOut->Length = region->Binding.Length;
    resultOut->TokenCapacity = capacity;
    TRACE("SHMDeviceRegionSessionCreate: created session %p with %u tokens", session, capacity);
    return OS_EOK;
}

// Hold session->Lock. A slot is free for teardown only when it has never run
// (Idle) or has completely finished (Finished).
static bool
__SessionHasNoTokensInUse(
    _In_ SHMDeviceRegionSession_t* session)
{
    bool noneInUse = true;

    for (unsigned int i = 0; i < session->Capacity; i++) {
        enum SHMDeviceRegionPhase phase = session->Tokens[i].Phase;

        if (phase != SHMDeviceRegionIdle && phase != SHMDeviceRegionFinished) {
            noneInUse = false;
            break;
        }
    }
    return noneInUse;
}

// Hold MappingLock and session->Lock. Teardown must wait while any token is in
// flight or while mapping recovery cleanup is running.
static bool
__SessionBusyLocked(
    _In_ SHMDeviceRegionSession_t* session)
{
    return session->Mapping->Lifetime.Cleaning || !__SessionHasNoTokensInUse(session);
}

// Hold MappingLock and make sure stop cleanup is excluded before unlinking.
static void
__UnlinkSession(
    _In_ SHMDeviceRegionSession_t* session)
{
    SHMDeviceRegions_t* regions = session->Mapping->Regions;
    SHMDeviceRegionSession_t** link = &regions->Sessions;

    while (*link != session) {
        assert(*link != NULL);
        link = &(*link)->Next;
    }
    *link = session->Next;
    assert(regions->SessionCount != 0);
    regions->SessionCount--;
}

// Unlink the session from its mapping once all tokens are idle or finished.
// When abandon is true (process exit) and tokens are still busy, revoke the
// device binding so hardware stops, detach record->Object, and mark the session
// abandoned so mapping recovery frees it once tokens drain. Normal closes
// return OS_EBUSY instead. *unlinkedOut is true only when removed here.
static oserr_t
__UnlinkDrainedSession(
    _In_  SHMRecord_t*              record,
    _In_  SHMDeviceRegionSession_t* session,
    _In_  bool                      abandon,
    _Out_ bool*                     unlinkedOut)
{
    SHMDeviceMapping_t* mapping = session->Mapping;
    bool                busy;

    *unlinkedOut = false;
    MutexLock(&mapping->Context->MappingLock);
    SpinlockAcquireIrq(&session->Lock);
    busy = __SessionBusyLocked(session);
    if (busy && abandon) {
        // Drop both locks before calling DMABindingRevoke because recovery
        // acquires locks while stopping tokens. Keep record->Object set until
        // after revoke so recovery will not collect an un-abandoned session,
        // then re-check whether tokens finished during revoke.
        MutexUnlock(&mapping->Context->MappingLock);
        SpinlockReleaseIrq(&session->Lock);
        DMABindingRevoke(mapping->Context->Binding);
        MutexLock(&mapping->Context->MappingLock);
        SpinlockAcquireIrq(&session->Lock);
        busy = __SessionBusyLocked(session);
    }
    if (busy) {
        if (abandon) {
            session->Abandoned = true;
            record->Object = NULL;
        }
        MutexUnlock(&mapping->Context->MappingLock);
        SpinlockReleaseIrq(&session->Lock);
        if (!abandon) {
            TRACE("__UnlinkDrainedSession: session %p still has tokens in use", session);
            return OS_EBUSY;
        }
        TRACE("__UnlinkDrainedSession: session %p abandoned with tokens in use", session);
        return OS_EOK;
    }
    __UnlinkSession(session);
    session->Unlinked = true;
    MutexUnlock(&mapping->Context->MappingLock);
    SpinlockReleaseIrq(&session->Lock);
    *unlinkedOut = true;
    return OS_EOK;
}

oserr_t
SHMDeviceRegionSessionDestroy(
    _In_ SHMRecord_t* record,
    _In_ bool         abandon)
{
    SHMDeviceRegionSession_t* current = record->Object;
    bool                      unlinked;
    oserr_t                   status;

    if (current == NULL) {
        return OS_EOK;
    }
    if (current->Mapping == NULL) {
        // Failed before linking to a mapping, so no mapping reference exists.
        record->Object = NULL;
        kfree(current);
        return OS_EOK;
    }
    assert(!current->Counted && current->Record == NULL);

    if (!current->Unlinked) {
        status = __UnlinkDrainedSession(record, current, abandon, &unlinked);
        if (status != OS_EOK) {
            if (status != OS_EBUSY) {
                ERROR("SHMDeviceRegionSessionDestroy: cannot unlink session %p: %u", current, status);
            }
            return status;
        }
        if (!unlinked) {
            return OS_EOK;
        }
    }

    // Wait until unlinking succeeds before dropping the mapping reference and
    // clearing record->Object. If an earlier close was interrupted before this,
    // keeping record->Object set lets a retried close find and finish it.
    SHMDeviceMappingRelease(&current->Mapping);
    record->Object = NULL;
    kfree(current);
    return OS_EOK;
}

// Find and unlink one abandoned session whose tokens have all drained. Returns
// NULL if none are ready or while mapping cleanup is running.
static SHMDeviceRegionSession_t*
__TakeAbandonedSession(
    _In_ SHMDeviceMapping_t* mapping)
{
    SHMDeviceRegionSession_t* session;

    MutexLock(&mapping->Context->MappingLock);
    if (mapping->Lifetime.Cleaning) {
        MutexUnlock(&mapping->Context->MappingLock);
        return NULL;
    }
    for (session = mapping->Regions->Sessions; session != NULL; session = session->Next) {
        bool removable;

        SpinlockAcquireIrq(&session->Lock);
        removable = session->Abandoned && __SessionHasNoTokensInUse(session);
        SpinlockReleaseIrq(&session->Lock);
        if (removable) {
            __UnlinkSession(session);
            break;
        }
    }
    MutexUnlock(&mapping->Context->MappingLock);
    return session;
}

void
SHMDeviceRegionsCollectSessions(
    _In_ SHMDeviceMapping_t* mapping)
{
    SHMDeviceRegionSession_t* session;

    // Run after Cleaning is cleared. MappingLock orders this pass against any
    // concurrent process exit, so a drained session is freed either here or by
    // SHMDeviceRegionSessionDestroy.
    while ((session = __TakeAbandonedSession(mapping)) != NULL) {
        SHMDeviceMapping_t* reference = mapping;

        TRACE("SHMDeviceRegionsCollectSessions: freeing abandoned session %p", session);
        kfree(session);
        SHMDeviceMappingRelease(&reference);
    }
}
