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

#include <shm/region.h>
#include <shm/view.h>
#include <arch/utils.h>
#include <arch/interrupts.h>
#include <ddk/barrier.h>
#include <handles/global.h>
#include <heap.h>
#include <limits.h>
#include <string.h>
#include "private.h"

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "Region admission requires lock-free integer atomics");

/** Canonical bounds and admission count. UINT_MAX denotes an exclusive writer. */
struct __SHMRegion {
    struct SHMRegionRange Range;
    atomic_uint          Users;
};

/** Fixed layout shared by all attachments. Buffer->Mutex protects publication. */
struct SHMRegionTable {
    unsigned int       Count;
    struct __SHMRegion Entries[];
};

/** Generation persists after end. Access zero denotes an idle token. */
struct __SHMRegionCpuToken {
    unsigned int      Generation;
    enum SHMCpuAccess Access;
};

/** Prepared slots and a global reference keeping their canonical region valid. */
struct SHMRegionCpuSession {
    uuid_t                     ReferenceID;
    GrantTokenScope_t          Scope;
    struct __SHMRegion*        Region;
    unsigned int               Capacity;
    struct __SHMRegionCpuToken Tokens[];
};

oserr_t
SHMRegionRegister(
    _In_ uuid_t                       shm,
    _In_ const struct SHMRegionRange* ranges,
    _In_ unsigned int                 count)
{
    SHMReference_t         source;
    struct SHMRegionTable* table;
    struct SHMBuffer*      buffer;
    size_t                 line = CpuDataCacheLineSize();
    size_t                 end = 0;
    oserr_t                status;

    if (count == 0 || count > SHM_REGION_LIMIT) {
        return OS_EINVALPARAMS;
    }
    status = SHMResolve(shm, &source);
    if (status != OS_EOK) {
        return status;
    }
    buffer = source.Buffer;
    
    // The layout covers the whole allocation and cannot be changed after it is
    // published, so every process that attaches later must use it. A view only
    // covers part of the allocation, and a read-only holder must not decide how
    // writers are admitted. Only an ordinary handle with write scope may register.
    status = OS_EPERMISSIONS;
    if (source.View != NULL || !(source.Scope & GrantTokenScopeWrite)) {
        goto exit;
    }

    // Region mode is chosen when the allocation is created. Allocations without
    // it are tracked by the legacy SHMAccess list, and that list does not check
    // the region table. Adding a table to such an allocation would let both
    // systems admit overlapping accesses without seeing each other.
    status = OS_ENOTSUPPORTED;
    if (!(buffer->Flags & SHM_REGIONS)) {
        goto exit;
    }

    if (line == 0) {
        line = 1;
    }

    // Each range must be nonempty, start after the previous range ends and lie
    // inside the allocation. Sorted, disjoint ranges mean one byte belongs to at
    // most one region, so admission on one region never needs to check another.
    // Cache-line alignment keeps cache maintenance for one region from touching
    // bytes that belong to a neighbor or a gap.
    status = OS_EINVALPARAMS;
    for (unsigned int i = 0; i < count; i++) {
        if (ranges[i].Length == 0 || ranges[i].Offset < end) {
            goto exit;
        }
        if (!__SHMRangeContains(0, buffer->Length, ranges[i].Offset, ranges[i].Length)) {
            goto exit;
        }
        if ((buffer->Offset + ranges[i].Offset) % line != 0 || ranges[i].Length % line != 0) {
            goto exit;
        }
        end = ranges[i].Offset + ranges[i].Length;
    }

    table = kmalloc(sizeof(*table) + count * sizeof(table->Entries[0]));
    if (table == NULL) {
        status = OS_EOOM;
        goto exit;
    }
    
    table->Count = count;
    for (unsigned int i = 0; i < count; i++) {
        table->Entries[i].Range = ranges[i];
        atomic_init(&table->Entries[i].Users, 0);
    }

    // The layout is published once. Open CPU sessions point into the current
    // table and its admission counters, so replacing it would leave them using
    // freed memory. A second register call fails and frees its own table.
    MutexLock(&buffer->Mutex);
    status = OS_EBUSY;
    if (buffer->Regions == NULL) {
        buffer->Regions = table;
        table = NULL;
        status = OS_EOK;
    }
    MutexUnlock(&buffer->Mutex);
    if (table != NULL) {
        kfree(table);
    }

exit:
    ReleaseGlobalHandle(source.GlobalHandleID);
    return status;
}

oserr_t
SHMRegionCpuCreate(
    _In_  uuid_t                       shm,
    _In_  unsigned int                 region,
    _In_  unsigned int                 capacity,
    _Out_ struct SHMRegionCpuSession** sessionOut,
    _Out_ OSBufferIdentity_t*          identityOut)
{
    SHMReference_t              source;
    struct SHMRegionTable*      table;
    struct __SHMRegion*         entry;
    struct SHMRegionCpuSession* session;
    oserr_t                     status;

    if (capacity == 0 || capacity > SHM_REGION_TOKEN_LIMIT) {
        return OS_EINVALPARAMS;
    }
    
    status = SHMResolve(shm, &source);
    if (status != OS_EOK) {
        return status;
    }
    
    // Every token gives at least read access to the region. Write scope is
    // checked later, when a write token is started.
    status = OS_EPERMISSIONS;
    if (!(source.Scope & GrantTokenScopeRead)) {
        goto error;
    }

    MutexLock(&source.Buffer->Mutex);
    table = source.Buffer->Regions;
    MutexUnlock(&source.Buffer->Mutex);
    // No layout has been registered yet, or the index is outside the layout.
    // Both mean the requested region does not exist.
    status = OS_ENOENT;
    if (table == NULL || region >= table->Count) {
        goto error;
    }
    
    // Admission is counted for the whole region. A view that covers only part
    // of it must not open a session, because a token would then admit or block
    // access to bytes the caller cannot reach through its own reference.
    entry = &table->Entries[region];
    status = OS_EPERMISSIONS;
    
    if (!__SHMRangeContains(source.Offset, source.Length, entry->Range.Offset, entry->Range.Length)) {
        goto error;
    }

    session = kmalloc(sizeof(*session) + capacity * sizeof(session->Tokens[0]));
    if (session == NULL) {
        status = OS_EOOM;
        goto error;
    }
    
    memset(session, 0, sizeof(*session) + capacity * sizeof(session->Tokens[0]));
    session->ReferenceID = source.GlobalHandleID;
    session->Scope = source.Scope;
    session->Region = entry;
    session->Capacity = capacity;
    
    identityOut->Buffer = source.Buffer->ID;
    identityOut->Region = region;
    identityOut->Generation = 1;
    
    *sessionOut = session;
    return OS_EOK;
error:
    ReleaseGlobalHandle(source.GlobalHandleID);
    return status;
}

oserr_t
SHMRegionCpuBegin(
    _In_ struct SHMRegionCpuSession* session,
    _In_ unsigned int                slot,
    _In_ unsigned int                generation,
    _In_ enum SHMCpuAccess           access)
{
    struct __SHMRegionCpuToken* token;
    unsigned int                users;
    unsigned int                desired;
    irqstate_t                  irqState;

    if (slot >= session->Capacity || generation == 0) {
        return OS_EINVALPARAMS;
    }

    if (access != SHMCpuAccessRead && access != SHMCpuAccessWrite) {
        return OS_EINVALPARAMS;
    }

    if (access == SHMCpuAccessWrite && !(session->Scope & GrantTokenScopeWrite)) {
        return OS_EPERMISSIONS;
    }

    token = &session->Tokens[slot];
    if (token->Access != 0) {
        return OS_EBUSY;
    }

    if (token->Generation == UINT_MAX) {
        return OS_EOVERFLOW;
    }

    if (generation != token->Generation + 1) {
        return OS_ENOENT;
    }

    users = atomic_load_explicit(&session->Region->Users, memory_order_relaxed);
    if (users == UINT_MAX || (access == SHMCpuAccessWrite && users != 0)) {
        return OS_EBUSY;
    }

    if (users == UINT_MAX - 1) {
        return OS_EOVERFLOW;
    }

    desired = access == SHMCpuAccessWrite ? UINT_MAX : users + 1;
    
    // Thread termination must not separate admission from token publication.
    // Keep interrupts disabled across both updates so process cleanup always
    // finds an active token for each admitted access.
    irqState = InterruptDisable();
    if (!atomic_compare_exchange_strong_explicit(&session->Region->Users, &users, desired,
            memory_order_acquire, memory_order_relaxed)) {
        InterruptRestoreState(irqState);
        return OS_EBUSY;
    }

    token->Generation = generation;
    token->Access = access;
    dma_mb();
    InterruptRestoreState(irqState);
    return OS_EOK;
}

oserr_t
SHMRegionCpuEnd(
    _In_ struct SHMRegionCpuSession* session,
    _In_ unsigned int                slot,
    _In_ unsigned int                generation)
{
    struct __SHMRegionCpuToken* token;
    irqstate_t                  irqState;

    if (slot >= session->Capacity || generation == 0) {
        return OS_EINVALPARAMS;
    }
    
    token = &session->Tokens[slot];
    if (token->Generation != generation || token->Access == 0) {
        return OS_ENOENT;
    }

    // Thread termination must not separate admission release from token clearing.
    // Keep interrupts disabled across both updates so process cleanup cannot
    // release the same access twice.
    irqState = InterruptDisable();
    dma_mb();
    if (token->Access == SHMCpuAccessWrite) {
        atomic_store_explicit(&session->Region->Users, 0, memory_order_release);
    } else {
        atomic_fetch_sub_explicit(&session->Region->Users, 1, memory_order_release);
    }
    token->Access = 0;
    InterruptRestoreState(irqState);
    return OS_EOK;
}

oserr_t
SHMRegionCpuDestroy(
    _InOut_ struct SHMRegionCpuSession** session,
    _In_    bool                         abandon)
{
    struct SHMRegionCpuSession* released = *session;

    for (unsigned int i = 0; i < released->Capacity; i++) {
        if (released->Tokens[i].Access != 0 && !abandon) {
            return OS_EBUSY;
        }
    }

    for (unsigned int i = 0; i < released->Capacity; i++) {
        if (released->Tokens[i].Access != 0) {
            // All process threads have stopped before abandon is called.
            SHMRegionCpuEnd(released, i, released->Tokens[i].Generation);
        }
    }

    *session = NULL;
    ReleaseGlobalHandle(released->ReferenceID);
    kfree(released);
    return OS_EOK;
}
