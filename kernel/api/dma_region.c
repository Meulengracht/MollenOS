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

#include <shm/device_region.h>
#include <memoryspace.h>
#include <debug.h>

static oserr_t
__ReleaseSession(
    _In_ SHMRecord_t* record)
{
    return SHMDeviceRegionSessionDestroy(record, false);
}

static oserr_t
__CleanupSession(
    _In_ SHMRecord_t* record)
{
    return SHMDeviceRegionSessionDestroy(record, true);
}

static const SHMRecordOps_t g_deviceRegionOps = {
    .Release = __ReleaseSession,
    .Cleanup = __CleanupSession,
    .CancelOperation = SHMDeviceRegionCancel
};

oserr_t
ScDmaRegionSessionOpen(
    _In_  uuid_t                    mapping,
    _In_  const OSBufferIdentity_t* userIdentity,
    _In_  unsigned int              capacity,
    _Out_ OSDmaRegionSession_t*     userResult)
{
    OSBufferIdentity_t    identity;
    OSDmaRegionSession_t  result = { 0 };
    SHMRecord_t*          parent;
    SHMRecord_t*          record = NULL;
    SHMDeviceOperation_t* operation = NULL;
    oserr_t               status;
    oserr_t               cleanupStatus;

    status = MemorySpaceCopyFromUser(&identity, userIdentity, sizeof(identity));
    if (status != OS_EOK) {
        return status;
    }

    status = SHMRecordBegin(mapping, NULL, &parent);
    if (status != OS_EOK) {
        return status;
    }

    if (!SHMDeviceIsMappingRecord(parent)) {
        status = OS_EINVALPARAMS;
        goto exit;
    }

    status = SHMDeviceMappingBeginOperation(
        parent,
        parent->Object,
        SHMDeviceOperationSubmit,
        &operation
    );
    if (status != OS_EOK) {
        goto exit;
    }

    status = SHMRecordCreate(&g_deviceRegionOps, &record);
    if (status != OS_EOK) {
        goto exit;
    }

    status = SHMDeviceRegionSessionCreate(
        parent->Object,
        &identity,
        capacity,
        record,
        &result
    );
    if (status != OS_EOK) {
        goto error;
    }

    result.Handle = record->Handle;
    status = MemorySpaceCopyToUser(userResult, &result, sizeof(result));
    if (status != OS_EOK) {
        goto error;
    }

    status = SHMDeviceOperationCheck(operation);
    if (status != OS_EOK) {
        goto error;
    }

    atomic_store(&record->State, SHMRecordIdle);
    goto exit;

error:
    cleanupStatus = SHMRecordCleanup(record);
    if (cleanupStatus != OS_EOK) {
        ERROR("ScDmaRegionSessionOpen: cleanup of resource %u failed: %u", record->Handle, cleanupStatus);
    }
    SHMRecordRemove(record);

exit:
    SHMDeviceOperationEnd(&operation);
    SHMRecordEnd(parent);
    return status;
}

oserr_t
ScDmaRegionBeginUse(
    _In_ uuid_t            handle,
    _In_ unsigned int      slot,
    _In_ unsigned int      generation,
    _In_ enum DMADirection direction)
{
    SHMRecord_t* record;
    oserr_t      status;

    status = SHMRecordBegin(handle, &g_deviceRegionOps, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMDeviceRegionBegin(
        record,
        record->Object,
        slot,
        generation,
        direction
    );
    SHMRecordEnd(record);
    return status;
}

oserr_t
ScDmaRegionEndUse(
    _In_ uuid_t       handle,
    _In_ unsigned int slot,
    _In_ unsigned int generation)
{
    SHMRecord_t* record;
    oserr_t      status;

    status = SHMRecordBegin(handle, &g_deviceRegionOps, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMDeviceRegionEnd(
        record,
        record->Object,
        slot,
        generation
    );
    SHMRecordEnd(record);
    return status;
}
