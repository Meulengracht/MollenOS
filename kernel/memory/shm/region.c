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
 * SHM region layouts and CPU access sessions. Buffers created with
 * SHM_REGIONS can be divided once into sorted, cache-line-aligned regions.
 * This file handles registering that layout, describing the regions covered by
 * a device mapping range, and managing CPU access sessions and tokens.
 */

#include <shm/region.h>
#include <shm/region_usage.h>
#include <shm/view.h>
#include <arch/utils.h>
#include <debug.h>
#include <handles/global.h>
#include <heap.h>
#include <string.h>
#include "private.h"

/** Bounds and usage of one region, shared by all sessions on it. */
struct __SHMRegion {
    struct SHMRegionRange Range;
    SHMRegionUsage_t      Usage;
};

/** Fixed layout shared by all attachments. Buffer->Mutex protects publication. */
struct SHMRegionTable {
    unsigned int       Count;
    struct __SHMRegion Entries[];
};

/** Prepared slots and a global reference keeping their canonical region valid. */
struct SHMRegionCpuSession {
    uuid_t                     ReferenceID;
    GrantTokenScope_t          Scope;
    struct __SHMRegion*        Region;
    unsigned int               Capacity;
    SHMRegionAccess_t          Tokens[];
};

// Checks that the ranges can be published as the layout of an allocation. Each
// range must be nonempty, start after the previous range ends and lie inside the
// allocation. Sorted, disjoint ranges mean one byte belongs to at most one
// region, so usage of one region never needs to check another. Cache-line
// alignment keeps cache maintenance for one region from touching bytes that
// belong to a neighbor or a gap. Returns OS_EINVALPARAMS if any range is bad.
static oserr_t
__ValidateRegionRanges(
    _In_ const struct SHMBuffer*      buffer,
    _In_ const struct SHMRegionRange* ranges,
    _In_ unsigned int                 count)
{
    size_t line = CpuDataCacheLineSize();
    size_t end = 0;

    if (line == 0) {
        line = 1;
    }

    for (unsigned int i = 0; i < count; i++) {
        if (ranges[i].Length == 0 || ranges[i].Offset < end) {
            WARNING("__ValidateRegionRanges: range %u is empty or overlaps the previous range", i);
            return OS_EINVALPARAMS;
        }

        if (!__SHMRangeContains(0, buffer->Length, ranges[i].Offset, ranges[i].Length)) {
            WARNING("__ValidateRegionRanges: range %u lies outside the allocation", i);
            return OS_EINVALPARAMS;
        }

        if ((buffer->Offset + ranges[i].Offset) % line != 0 || ranges[i].Length % line != 0) {
            WARNING("__ValidateRegionRanges: range %u is not aligned to the %zu byte cache line", i, line);
            return OS_EINVALPARAMS;
        }

        end = ranges[i].Offset + ranges[i].Length;
    }
    return OS_EOK;
}

// Allocates a region table for the ranges. Returns NULL if memory ran out.
static struct SHMRegionTable*
__CreateRegionTable(
    _In_ const struct SHMRegionRange* ranges,
    _In_ unsigned int                 count)
{
    struct SHMRegionTable* table;

    table = kmalloc(sizeof(*table) + count * sizeof(table->Entries[0]));
    if (table == NULL) {
        return NULL;
    }

    table->Count = count;
    for (unsigned int i = 0; i < count; i++) {
        table->Entries[i].Range = ranges[i];
        SHMRegionUsageInitialize(&table->Entries[i].Usage);
    }
    return table;
}

oserr_t
SHMRegionDescribeRange(
    _In_  struct SHMBuffer*    buffer,
    _In_  size_t              offset,
    _In_  size_t              length,
    _Out_ SHMRegionBinding_t* bindings,
    _In_  unsigned int        capacity,
    _Out_ unsigned int*       countOut)
{
    struct SHMRegionTable* table;
    unsigned int          first;
    unsigned int          count = 0;
    size_t                end;
    size_t                position = offset;
    oserr_t               status = OS_EINVALPARAMS;

    if (length == 0 || offset > SIZE_MAX - length) {
        return OS_EINVALPARAMS;
    }
    end = offset + length;

    MutexLock(&buffer->Mutex);
    table = buffer->Regions;
    if (!(buffer->Flags & SHM_REGIONS) || table == NULL) {
        status = OS_ENOTSUPPORTED;
        goto unlock;
    }

    // Find the region starting at offset, then walk forward to confirm the
    // table covers [offset, end) completely with no gaps or partial regions.
    for (first = 0; first < table->Count; first++) {
        if (table->Entries[first].Range.Offset == offset) {
            break;
        }
    }
    for (unsigned int i = first; i < table->Count && position < end; i++) {
        const struct SHMRegionRange* range = &table->Entries[i].Range;

        if (range->Offset != position || range->Length > end - position) {
            goto unlock;
        }
        position += range->Length;
        count++;
    }
    if (position != end) {
        goto unlock;
    }

    // Callers pass bindings as NULL first to query the required slot count.
    if (bindings != NULL && capacity < count) {
        status = OS_EBUFFER;
        goto unlock;
    }
    if (bindings != NULL) {
        for (unsigned int i = 0; i < count; i++) {
            struct __SHMRegion* region = &table->Entries[first + i];

            // Region layouts never change once published, so Generation is 1.
            bindings[i] = (SHMRegionBinding_t) {
                .Identity = { .Buffer = buffer->ID, .Region = first + i, .Generation = 1 },
                .Usage = &region->Usage,
                .Offset = region->Range.Offset,
                .Length = region->Range.Length
            };
        }
    }
    *countOut = count;
    status = OS_EOK;
unlock:
    MutexUnlock(&buffer->Mutex);
    return status;
}

oserr_t
SHMRegionRegister(
    _In_ uuid_t                       shm,
    _In_ const struct SHMRegionRange* ranges,
    _In_ unsigned int                 count)
{
    SHMReference_t         source;
    struct SHMRegionTable* table;
    struct SHMBuffer*      buffer;
    oserr_t                status;

    if (count == 0 || count > SHM_REGION_LIMIT) {
        WARNING("SHMRegionRegister: region count %u for buffer %u is not between 1 and %u",
                count, shm, (unsigned int)SHM_REGION_LIMIT);
        return OS_EINVALPARAMS;
    }

    status = SHMResolve(shm, &source);
    if (status != OS_EOK) {
        WARNING("SHMRegionRegister: SHMResolve buffer %u failed: %u", shm, status);
        return status;
    }
    buffer = source.Buffer;

    // The layout covers the whole allocation and cannot be changed after it is
    // published, so every process that attaches later must use it. A view only
    // covers part of the allocation, and a read-only holder must not decide how
    // writers are admitted. Only an ordinary handle with write scope may register.
    if (source.View != NULL || !(source.Scope & GrantTokenScopeWrite)) {
        WARNING("SHMRegionRegister: buffer %u needs an ordinary handle with write scope", shm);
        status = OS_EPERMISSIONS;
        goto exit;
    }

    // Region mode is chosen when the allocation is created. Allocations without
    // it are tracked by the legacy SHMAccess list, and that list does not check
    // the region table. Adding a table to such an allocation would let both
    // systems admit overlapping accesses without seeing each other.
    if (!(buffer->Flags & SHM_REGIONS)) {
        WARNING("SHMRegionRegister: buffer %u was not created with region support", shm);
        status = OS_ENOTSUPPORTED;
        goto exit;
    }

    status = __ValidateRegionRanges(buffer, ranges, count);
    if (status != OS_EOK) {
        goto exit;
    }

    table = __CreateRegionTable(ranges, count);
    if (table == NULL) {
        ERROR("SHMRegionRegister: region table allocation for buffer %u failed", shm);
        status = OS_EOOM;
        goto exit;
    }

    // The layout is published once. Open CPU sessions point into the current
    // table and its usage counters, so replacing it would leave them using
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
        WARNING("SHMRegionRegister: buffer %u already has a region layout", shm);
        kfree(table);
    } else {
        TRACE("SHMRegionRegister: buffer %u registered with %u regions", shm, count);
    }

exit:
    ReleaseGlobalHandle(source.GlobalHandleID);
    return status;
}

// Finds the region entry for a session. The buffer must have a registered
// layout and the reference must cover the whole region. Usage is counted
// for the whole region, so a view that covers only part of it must not open a
// session. A token would then admit or block access to bytes the caller cannot
// reach through its own reference. Returns OS_ENOENT if no layout is registered
// or the index is outside it, or OS_EPERMISSIONS for a partial reference.
static oserr_t
__FindRegionEntry(
    _In_  const SHMReference_t* source,
    _In_  unsigned int          region,
    _Out_ struct __SHMRegion**  entryOut)
{
    struct SHMRegionTable* table;
    struct __SHMRegion*    entry;

    MutexLock(&source->Buffer->Mutex);
    table = source->Buffer->Regions;
    MutexUnlock(&source->Buffer->Mutex);
    if (table == NULL || region >= table->Count) {
        return OS_ENOENT;
    }

    entry = &table->Entries[region];
    if (!__SHMRangeContains(source->Offset, source->Length, entry->Range.Offset, entry->Range.Length)) {
        return OS_EPERMISSIONS;
    }
    *entryOut = entry;
    return OS_EOK;
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
    struct __SHMRegion*         entry;
    struct SHMRegionCpuSession* session;
    size_t                      size;
    oserr_t                     status;

    if (capacity == 0 || capacity > SHM_REGION_TOKEN_LIMIT) {
        WARNING("SHMRegionCpuCreate: token capacity %u for buffer %u is not between 1 and %u",
                capacity, shm, (unsigned int)SHM_REGION_TOKEN_LIMIT);
        return OS_EINVALPARAMS;
    }

    status = SHMResolve(shm, &source);
    if (status != OS_EOK) {
        WARNING("SHMRegionCpuCreate: SHMResolve buffer %u failed: %u", shm, status);
        return status;
    }

    // Every token gives at least read access to the region. Write scope is
    // checked later, when a write token is started.
    if (!(source.Scope & GrantTokenScopeRead)) {
        WARNING("SHMRegionCpuCreate: buffer %u needs read scope", shm);
        status = OS_EPERMISSIONS;
        goto error;
    }

    status = __FindRegionEntry(&source, region, &entry);
    if (status != OS_EOK) {
        WARNING("SHMRegionCpuCreate: region %u of buffer %u is not available: %u", region, shm, status);
        goto error;
    }

    size = sizeof(*session) + capacity * sizeof(session->Tokens[0]);
    session = kmalloc(size);
    if (session == NULL) {
        ERROR("SHMRegionCpuCreate: session allocation for buffer %u failed", shm);
        status = OS_EOOM;
        goto error;
    }

    memset(session, 0, size);
    session->ReferenceID = source.GlobalHandleID;
    session->Scope = source.Scope;
    session->Region = entry;
    session->Capacity = capacity;

    identityOut->Buffer = source.Buffer->ID;
    identityOut->Region = region;
    identityOut->Generation = 1;

    *sessionOut = session;
    TRACE("SHMRegionCpuCreate: session opened on region %u of buffer %u with %u tokens", region, shm, capacity);
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
    if (slot >= session->Capacity || generation == 0) {
        WARNING("SHMRegionCpuBegin: slot %u or generation %u is invalid, capacity is %u",
                slot, generation, session->Capacity);
        return OS_EINVALPARAMS;
    }
    
    if (access != SHMCpuAccessRead && access != SHMCpuAccessWrite) {
        WARNING("SHMRegionCpuBegin: access kind %u is invalid", (unsigned int)access);
        return OS_EINVALPARAMS;
    }

    if (access == SHMCpuAccessWrite && !(session->Scope & GrantTokenScopeWrite)) {
        WARNING("SHMRegionCpuBegin: write access refused, the session has no write scope");
        return OS_EPERMISSIONS;
    }

    return SHMRegionUsageBegin(
        &session->Region->Usage,
        &session->Tokens[slot],
        generation,
        access == SHMCpuAccessWrite
    );
}

oserr_t
SHMRegionCpuEnd(
    _In_ struct SHMRegionCpuSession* session,
    _In_ unsigned int                slot,
    _In_ unsigned int                generation)
{
    if (slot >= session->Capacity || generation == 0) {
        WARNING("SHMRegionCpuEnd: slot %u or generation %u is invalid, capacity is %u",
                slot, generation, session->Capacity);
        return OS_EINVALPARAMS;
    }
    return SHMRegionUsageEnd(&session->Tokens[slot], generation);
}

oserr_t
SHMRegionCpuDestroy(
    _InOut_ struct SHMRegionCpuSession** session,
    _In_    bool                         abandon)
{
    struct SHMRegionCpuSession* released = *session;

    for (unsigned int i = 0; i < released->Capacity; i++) {
        if (released->Tokens[i].State != SHMRegionAccessIdle && !abandon) {
            WARNING("SHMRegionCpuDestroy: session still has an access in slot %u", i);
            return OS_EBUSY;
        }
    }

    for (unsigned int i = 0; i < released->Capacity; i++) {
        if (released->Tokens[i].State != SHMRegionAccessIdle) {
            // During process teardown, every thread in the process has already
            // stopped before abandon is true, so remaining tokens can end here.
            oserr_t status = SHMRegionCpuEnd(released, i, released->Tokens[i].Generation);
            if (status != OS_EOK) {
                ERROR("SHMRegionCpuDestroy: ending the access in slot %u failed: %u", i, status);
                return status;
            }
        }
    }

    *session = NULL;
    ReleaseGlobalHandle(released->ReferenceID);
    kfree(released);
    return OS_EOK;
}
