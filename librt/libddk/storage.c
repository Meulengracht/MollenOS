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

#include <internal/_storage.h>
#include <internal/_gracht.h>
#include <internal/_shm_transfer.h>
#include <internal/_utils.h>
#include <gracht/link/vali.h>
#include <os/usched/job.h>
#include <time.h>
#include <ctt_storage_service_client.h>

// A storage driver answers a transfer with OS_EBUSY when it has no room for
// the request right now, for example when its request queue is full. The
// transfer is sent again every STORAGE_BUSY_RETRY_DELAY_MS milliseconds, at
// most STORAGE_BUSY_RETRY_COUNT times. That gives the driver 1 second in total
// to make room before the caller gets OS_EBUSY.
#define STORAGE_BUSY_RETRY_DELAY_MS 20
#define STORAGE_BUSY_RETRY_COUNT    50

// Sends one transfer request to the driver and waits for its answer. The
// grant token for the buffer range is created here and revoked before
// returning, so each attempt uses its own token. driverBusy is set when the
// driver answered OS_EBUSY, and is false for every local or IPC failure.
static oserr_t
__TryTransfer(
    _In_  uuid_t              driverID,
    _In_  uuid_t              deviceID,
    _In_  uuid_t              bufferID,
    _In_  size_t              offset,
    _In_  const UInteger64_t* sector,
    _In_  size_t              sectorSize,
    _In_  size_t              count,
    _In_  bool                write,
    _Out_ size_t*             completed,
    _Out_ bool*               driverBusy)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverID);
    struct sys_shm_transfer  description;
    SHMTransfer_t            transfer;
    size_t                   result = 0;
    oserr_t                  status;

    *completed = 0;
    *driverBusy = false;

    status = __SHMTransferCreate(
        bufferID,
        offset,
        count * sectorSize,
        write ? GrantTokenScopeRead : GrantTokenScopeRead | GrantTokenScopeWrite,
        &transfer
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __ToSysSHMTransfer(&transfer, &description);
    if (status != OS_EOK) {
        goto cleanup;
    }

    if (ctt_storage_transfer(GetGrachtClient(), &msg.base, deviceID,
                             write ? SYS_TRANSFER_DIRECTION_WRITE : SYS_TRANSFER_DIRECTION_READ,
                             sector->u.LowPart, sector->u.HighPart, &description, count) != 0) {
        status = OS_EUNKNOWN;
        goto cleanup;
    }

    status = __GrachtAwaitCompleted(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    if (status != OS_EOK) {
        goto cleanup;
    }

    if (ctt_storage_transfer_result(GetGrachtClient(), &msg.base, &status, &result) != 0 || result > count) {
        status = OS_EPROTOCOL;
        goto cleanup;
    }

    *completed = result;
    *driverBusy = status == OS_EBUSY;

cleanup:
    OSGrantTokenRevoke(transfer.Token.ControlHandleID);
    return status;
}

// Puts the calling job to sleep for STORAGE_BUSY_RETRY_DELAY_MS. Callers run
// in usched jobs (filed and the filesystem modules), so other jobs in the
// process keep running while this one waits.
static oserr_t
__RetryDelay(void)
{
    struct timespec now;
    struct timespec wakeUp;

    struct timespec delay = {
        .tv_sec = 0,
        .tv_nsec = STORAGE_BUSY_RETRY_DELAY_MS * NSEC_PER_MSEC
    };

    if (timespec_get(&now, TIME_UTC) != TIME_UTC) {
        return OS_EUNKNOWN;
    }

    timespec_add(&now, &delay, &wakeUp);
    if (usched_job_sleep(&wakeUp) != 0) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

oserr_t
__StorageTransfer(
    _In_  uuid_t              driverID,
    _In_  uuid_t              deviceID,
    _In_  uuid_t              bufferID,
    _In_  size_t              offset,
    _In_  const UInteger64_t* sector,
    _In_  size_t              sectorSize,
    _In_  size_t              count,
    _In_  bool                write,
    _Out_ size_t*             completed)
{
    bool    driverBusy;
    int     retries;
    oserr_t status;

    // reset even on failures
    *completed = 0;

    if (count == 0) {
        return OS_EOK;
    }

    if (sectorSize == 0 || count > UINT32_MAX / sectorSize) {
        return OS_EINVALPARAMS;
    }

    // Only a busy answer from the driver is tried again. A local failure, an
    // IPC failure or any other driver result goes straight to the caller.
    // completed always holds the sector count of the last attempt.
    for (retries = 0; ; retries++) {
        status = __TryTransfer(
            driverID, deviceID, bufferID, offset, sector,
            sectorSize, count, write, completed, &driverBusy
        );
        if (!driverBusy || retries == STORAGE_BUSY_RETRY_COUNT) {
            return status;
        }

        status = __RetryDelay();
        if (status != OS_EOK) {
            return status;
        }
    }
}
