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
 */

#include <shm/access.h>
#include <shm/view.h>
#include <arch/utils.h>
#include <assert.h>
#include <debug.h>
#include <futex.h>
#include <handles/global.h>
#include <handles/handle.h>
#include <heap.h>
#include <string.h>

#include "private.h"
#include "../utils.h"

void
SHMAccessInitialize(
    _Out_ struct SHMAccess* access,
    _In_  struct SHMBuffer* buffer,
    _In_  size_t            offset,
    _In_  size_t            length)
{
    memset(access, 0, sizeof(*access));
    access->Buffer = buffer;
    access->ReferenceID = buffer->ID;
    access->Scope = GrantTokenScopeRead | GrantTokenScopeWrite;
    access->Offset = offset;
    access->Length = length;
    atomic_init(&access->Event, 0);
}

static void
__AccessSetBounds(
    _InOut_ struct SHMAccess* access,
    _In_    size_t            offset,
    _In_    size_t            length)
{
    size_t line = CpuDataCacheLineSize();
    size_t start = access->Buffer->Offset + access->Offset + offset;

    // Round access bounds to cache lines, including CPU-only attachments.
    // Inclusive endpoints avoid overflow when the range ends at SIZE_MAX.
    if (line == 0) {
        line = 1;
    }

    access->First = start & ~(line - 1);
    access->Last = (start + length - 1) | (line - 1);
}

oserr_t
SHMAccessCreate(
    _In_  uuid_t             shm,
    _In_  size_t             offset,
    _In_  size_t             length,
    _Out_ struct SHMAccess** accessOut)
{
    SHMReference_t    source;
    struct SHMBuffer* buffer;
    struct SHMAccess* access;
    oserr_t           status;

    status = SHMResolve(shm, &source);
    if (status != OS_EOK) {
        return status;
    }

    buffer = source.Buffer;

    // Region-backed buffers need region-aware access tracking.
    if (buffer->Flags & SHM_REGIONS) {
        __ReleaseMemoryHandleReference(source.GlobalHandleID, __func__);
        return OS_ENOTSUPPORTED;
    }

    if (length == 0 || !__SHMRangeContains(0, source.Length, offset, length)) {
        __ReleaseMemoryHandleReference(source.GlobalHandleID, __func__);
        return OS_EINVALPARAMS;
    }
    
    access = kmalloc(sizeof(struct SHMAccess));
    if (access == NULL) {
        __ReleaseMemoryHandleReference(source.GlobalHandleID, __func__);
        return OS_EOOM;
    }

    SHMAccessInitialize(access, buffer, source.Offset + offset, length);
    access->ReferenceID = source.GlobalHandleID;
    access->Scope = source.Scope;
    
    *accessOut = access;
    return OS_EOK;
}

// The caller must hold the buffer mutex for all list checks and changes below.
static void
__AccessAppend(
    _In_ struct SHMAccess* access)
{
    struct SHMBuffer* buffer = access->Buffer;

    if (buffer->Access.Last != NULL) {
        buffer->Access.Last->Next = access;
    } else {
        buffer->Access.First = access;
    }
    buffer->Access.Last = access;
}

static bool
__AccessOverlaps(
    _In_ const struct SHMAccess* left,
    _In_ const struct SHMAccess* right)
{
    return left->First <= right->Last && right->First <= left->Last;
}

static oserr_t
__AccessCheck(
    _In_ struct SHMAccess* access)
{
    struct SHMAccess* other;
    bool              earlier = true;

    access->BlockedBy = NULL;
    for (other = access->Buffer->Access.First; other != NULL; other = other->Next) {
        if (other == access) {
            earlier = false;
            continue;
        }

        if (!__AccessOverlaps(other, access)) {
            continue;
        }

        if (other->Phase == SHMAccessQuarantined) {
            return OS_EDEVFAULT;
        }

        if ((earlier || other->Phase == SHMAccessActive) && (other->Write || access->Write)) {
            // Link behind the last conflicting access so queued writers wake up
            // one after another in order, avoiding a stampede on the active writer.
            access->BlockedBy = other;
        }
    }
    return access->BlockedBy != NULL ? OS_EBUSY : OS_EOK;
}

static void
__AccessRemove(
    _In_ struct SHMAccess* access)
{
    struct SHMAccess** link = &access->Buffer->Access.First;
    struct SHMAccess*  previous = NULL;

    while (*link != access) {
        previous = *link;
        link = &previous->Next;
    }
    *link = access->Next;

    if (access->Buffer->Access.Last == access) {
        access->Buffer->Access.Last = previous;
    }
    
    if (access->Phase == SHMAccessWaiting) {
        atomic_fetch_sub(&access->Buffer->Access.Waiting, 1);
    }
    
    if (access->Phase == SHMAccessQuarantined) {
        atomic_fetch_sub(&access->Buffer->Access.Faults, 1);
    }
    
    access->Next = NULL;
    access->BlockedBy = NULL;
    access->Phase = SHMAccessIdle;
    access->Holder = SHMAccessHolderNone;
}

static bool
__AccessAffected(
    _In_ const struct SHMAccess* access,
    _In_ const struct SHMAccess* changed)
{
    if (access->BlockedBy == changed) {
        return true;
    }

    if (changed->Phase != SHMAccessQuarantined) {
        return false;
    }

    // Report a new fault even if another reservation blocks access.
    // This includes overlapping readers that can otherwise run together.
    return __AccessOverlaps(access, changed);
}

static void
__AccessWake(
    _In_ struct SHMBuffer* buffer,
    _In_ struct SHMAccess* changed)
{
    struct SHMAccess* access;
    oserr_t           wakeStatus;

    if (atomic_load(&buffer->Access.Waiting) == 0) {
        return;
    }

    for (access = buffer->Access.First; access != NULL; access = access->Next) {
        if (access->Phase != SHMAccessWaiting || !__AccessAffected(access, changed)) {
            continue;
        }
        if (__AccessCheck(access) == OS_EBUSY) {
            continue;
        }

        atomic_fetch_add(&access->Event, 1);
        // Keep the buffer mutex locked until FutexWake returns so the woken
        // caller cannot free this struct while FutexWake reads Event. Calls on
        // one reservation never overlap, so at most one waiter sleeps here.
        wakeStatus = FutexWake(&access->Event, 1, 0);

        // A caller may observe the changed event before it starts sleeping.
        if (wakeStatus != OS_EOK && wakeStatus != OS_ENOENT) {
            ERROR("%s: FutexWake event %p failed: %u", __func__, (void*)&access->Event, wakeStatus);
        }
    }
}

// While the range is blocked, retry an interrupted wait only if a range wake
// changed the event sequence. Otherwise report the interrupt. Other wait errors
// stop the wait even if the sequence changed.
static bool
__WaitFailed(
    _In_ oserr_t           waitStatus,
    _In_ struct SHMAccess* access,
    _In_ int               event)
{
    if (waitStatus == OS_EOK) {
        return false;
    }

    if (waitStatus != OS_EINTERRUPTED) {
        return true;
    }

    return atomic_load(&access->Event) == event;
}

oserr_t
SHMAccessBegin(
    _In_ struct SHMAccess*           access,
    _In_ size_t                      offset,
    _In_ size_t                      length,
    _In_ enum SHMAccessHolder        holder,
    _In_ bool                        write,
    _In_ const struct SHMAccessWait* wait)
{
    struct SHMBuffer*    buffer = access->Buffer;
    OSAsyncContext_t*    async = wait != NULL ? wait->Async : NULL;
    const OSTimestamp_t* deadline = wait != NULL ? wait->Deadline : NULL;
    bool                 blocking = wait != NULL && wait->Blocking;
    oserr_t              status;
    oserr_t              waitStatus = OS_EOK;
    int                  event = 0;

    if (buffer->Flags & SHM_REGIONS) {
        return OS_ENOTSUPPORTED;
    }

    if (holder == SHMAccessHolderCpu) {
        if (!(access->Scope & GrantTokenScopeRead) ||
            (write && !(access->Scope & GrantTokenScopeWrite))) {
            return OS_EPERMISSIONS;
        }
    }

    assert(holder == SHMAccessHolderCpu || holder == SHMAccessHolderDevice);
    assert(length != 0 && __SHMRangeContains(0, access->Length, offset, length));

    MutexLock(&buffer->Mutex);
    if (access->Cancelled) {
        MutexUnlock(&buffer->Mutex);
        return OS_ECANCELLED;
    }

    if (access->Phase != SHMAccessIdle) {
        MutexUnlock(&buffer->Mutex);
        return OS_EBUSY;
    }
    
    __AccessSetBounds(access, offset, length);
    access->Deadline = deadline != NULL ? *deadline : *OS_DEADLINE_INFINITE;
    access->Write = write;
    access->Holder = holder;
    access->Phase = SHMAccessPending;
    __AccessAppend(access);

    for (;;) {
        // Check Cancelled under the same mutex before granting access. A wake
        // must not grant access after cancellation, including when a background
        // thread resumes this call after FutexWait returned OS_EFORKED.
        if (access->Cancelled) {
            status = OS_ECANCELLED;
            break;
        }

        status = __AccessCheck(access);
        if (status != OS_EBUSY || !blocking) {
            break;
        }

        if (__WaitFailed(waitStatus, access, event)) {
            status = waitStatus;
            break;
        }
        
        if (access->Phase != SHMAccessWaiting) {
            access->Phase = SHMAccessWaiting;
            atomic_fetch_add(&buffer->Access.Waiting, 1);
        }
        
        event = atomic_load(&access->Event);
        MutexUnlock(&buffer->Mutex);
        waitStatus = FutexWait(async, &access->Event, event, 0, NULL, 0, 0, &access->Deadline);
        if (waitStatus == OS_EFORKED) {
            return waitStatus;
        }
        async = NULL;
        MutexLock(&buffer->Mutex);
    }
    
    if (status == OS_EOK) {
        if (access->Phase == SHMAccessWaiting) {
            atomic_fetch_sub(&buffer->Access.Waiting, 1);
        }
        access->Phase = SHMAccessActive;
    } else if (access->Phase != SHMAccessQuarantined) {
        // Locking the range (quarantine) already lowered the waiting count and
        // must keep the range blocked until hardware stop cleanup runs.
        __AccessRemove(access);
        __AccessWake(buffer, access);
    }
    MutexUnlock(&buffer->Mutex);
    
    return status;
}

oserr_t
SHMAccessExtend(
    _In_ struct SHMAccess* access)
{
    struct SHMAccess* other;
    oserr_t           status = OS_EOK;

    // Cancellation and extension must be ordered by the buffer mutex even
    // when no other range is queued. The common case still skips the list walk.
    MutexLock(&access->Buffer->Mutex);
    if (access->Cancelled) {
        status = OS_ECANCELLED;
        goto exit;
    }

    if (atomic_load(&access->Buffer->Access.Waiting) == 0 &&
        atomic_load(&access->Buffer->Access.Faults) == 0) {
        goto exit;
    }

    for (other = access->Buffer->Access.First; other != NULL; other = other->Next) {
        if (!__AccessOverlaps(other, access)) {
            continue;
        }
        
        if (other->Phase == SHMAccessQuarantined) {
            status = OS_EDEVFAULT;
            break;
        }
        
        if (other->Phase == SHMAccessActive || other == access) {
            continue;
        }
        
        if (other->Write || access->Write) {
            status = OS_EBUSY;
            break;
        }
    }

exit:
    MutexUnlock(&access->Buffer->Mutex);

    return status;
}

void
SHMAccessEnd(
    _In_ struct SHMAccess* access)
{
    MutexLock(&access->Buffer->Mutex);
    if (access->Phase != SHMAccessIdle && access->Phase != SHMAccessQuarantined) {
        __AccessRemove(access);
        __AccessWake(access->Buffer, access);
    }
    MutexUnlock(&access->Buffer->Mutex);
}

// Hold Buffer->Mutex. Wake this reservation right away, since the access
// blocking it could stay active indefinitely.
static void
__AccessCancelLocked(
    _In_ SHMAccess_t* access)
{
    if (access->Cancelled) {
        return;
    }

    access->Cancelled = true;
    atomic_fetch_add(&access->Event, 1);
    if (access->Phase == SHMAccessWaiting) {
        oserr_t wakeStatus = FutexWake(&access->Event, 1, 0);

        // A caller may observe the changed event before it starts sleeping.
        if (wakeStatus != OS_EOK && wakeStatus != OS_ENOENT) {
            ERROR("%s: FutexWake event %p failed: %u", __func__, (void*)&access->Event, wakeStatus);
        }
    }
}

void
SHMAccessCancel(
    _In_ SHMAccess_t* access)
{
    MutexLock(&access->Buffer->Mutex);
    __AccessCancelLocked(access);
    MutexUnlock(&access->Buffer->Mutex);
}

enum SHMAccessPhase
SHMAccessGetPhase(
    _In_ SHMAccess_t* access)
{
    enum SHMAccessPhase phase;

    MutexLock(&access->Buffer->Mutex);
    phase = access->Phase;
    MutexUnlock(&access->Buffer->Mutex);
    return phase;
}

void
SHMAccessQuarantine(
    _In_ struct SHMAccess* access)
{
    MutexLock(&access->Buffer->Mutex);
    
    __AccessCancelLocked(access);
    if (access->Phase == SHMAccessIdle) {
        __AccessAppend(access);
    }

    if (access->Phase == SHMAccessWaiting) {
        atomic_fetch_sub(&access->Buffer->Access.Waiting, 1);
    }

    if (access->Phase != SHMAccessQuarantined) {
        atomic_fetch_add(&access->Buffer->Access.Faults, 1);
    }

    // When a binding is lost, we cannot assume the device stopped at the last
    // submitted range. Block the entire mapping, including any idle part.
    __AccessSetBounds(access, 0, access->Length);
    access->Holder = SHMAccessHolderDevice;
    access->Phase = SHMAccessQuarantined;
    __AccessWake(access->Buffer, access);
    MutexUnlock(&access->Buffer->Mutex);
}

void
SHMAccessCompleteStop(
    _In_ SHMAccess_t* access)
{
    // By now the hardware is confirmed stopped, all background waiters have
    // exited, and caches are synced. Keep Cancelled set so this reservation
    // cannot be reused after removing it from the active list.
    MutexLock(&access->Buffer->Mutex);
    if (access->Phase != SHMAccessIdle) {
        __AccessRemove(access);
        __AccessWake(access->Buffer, access);
    }
    MutexUnlock(&access->Buffer->Mutex);
}

oserr_t
SHMAccessDestroy(
    _InOut_ struct SHMAccess** access)
{
    struct SHMAccess* released = *access;

    MutexLock(&released->Buffer->Mutex);
    if (released->Phase != SHMAccessIdle) {
        MutexUnlock(&released->Buffer->Mutex);
        return OS_EBUSY;
    }
    MutexUnlock(&released->Buffer->Mutex);

    *access = NULL;
    
    __ReleaseMemoryHandleReference(released->ReferenceID, __func__);
    kfree(released);
    return OS_EOK;
}
