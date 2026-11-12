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
#include <shm/record.h>
#include <memoryspace.h>
#include <heap.h>

static oserr_t
__ReleaseRegionSession(
    _In_    struct SHMRecord* record,
    _InOut_ void**            object)
{
    struct SHMRegionCpuSession* session = *object;
    oserr_t                     status;
    (void)record;

    status = SHMRegionCpuDestroy(&session, false);
    *object = session;
    return status;
}

static oserr_t
__AbandonRegionSession(
    _In_    struct SHMRecord* record,
    _InOut_ void**            object)
{
    struct SHMRegionCpuSession* session = *object;
    oserr_t                     status;
    (void)record;

    status = SHMRegionCpuDestroy(&session, true);
    *object = session;
    return status;
}

static const struct SHMRecordOps g_regionSessionOps = {
    .Release = __ReleaseRegionSession,
    .Abandon = __AbandonRegionSession
};

oserr_t
ScShmRegionsRegister(
    _In_ uuid_t                       shm,
    _In_ const struct SHMRegionRange* userRanges,
    _In_ unsigned int                 count)
{
    struct SHMRegionRange* ranges;
    oserr_t                status;
    size_t                 size;

    if (count == 0 || count > SHM_REGION_LIMIT) {
        return OS_EINVALPARAMS;
    }
    
    size = count * sizeof(struct SHMRegionRange);
    ranges = kmalloc(size);
    if (ranges == NULL) {
        return OS_EOOM;
    }
    
    status = MemorySpaceCopyFromUser(ranges, userRanges, size);
    if (status == OS_EOK) {
        status = SHMRegionRegister(shm, ranges, count);
    }
    kfree(ranges);
    return status;
}

oserr_t
ScShmRegionSessionOpen(
    _In_  uuid_t                   shm,
    _In_  unsigned int             region,
    _In_  unsigned int             capacity,
    _Out_ struct SHMRegionSession* userResult)
{
    struct SHMRegionSession     result = { 0 };
    struct SHMRecord*           record;
    struct SHMRegionCpuSession* session;
    oserr_t                     status;

    status = SHMRecordCreate(&g_regionSessionOps, &record);
    if (status != OS_EOK) {
        return status;
    }
    
    status = SHMRegionCpuCreate(
        shm,
        region,
        capacity,
        &session,
        &result.Identity
    );
    if (status != OS_EOK) {
        goto error;
    }
    
    record->Object = session;
    result.Handle = record->Handle;
    result.TokenCapacity = capacity;
    
    status = MemorySpaceCopyToUser(userResult, &result, sizeof(result));
    if (status != OS_EOK) {
        goto error;
    }

    atomic_store(&record->State, SHMRecordIdle);
    return OS_EOK;

error:
    SHMRecordRelease(record);
    SHMRecordRemove(record);
    return status;
}

oserr_t
ScShmRegionBeginCpu(
    _In_ uuid_t            handle,
    _In_ unsigned int      slot,
    _In_ unsigned int      generation,
    _In_ enum SHMCpuAccess access)
{
    struct SHMRecord* record;
    oserr_t           status;

    status = SHMRecordBegin(handle, &g_regionSessionOps, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMRegionCpuBegin(record->Object, slot, generation, access);
    SHMRecordEnd(record);
    return status;
}

oserr_t
ScShmRegionEndCpu(
    _In_ uuid_t       handle,
    _In_ unsigned int slot,
    _In_ unsigned int generation)
{
    struct SHMRecord* record;
    oserr_t           status;

    status = SHMRecordBegin(handle, &g_regionSessionOps, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMRegionCpuEnd(record->Object, slot, generation);
    SHMRecordEnd(record);
    return status;
}
