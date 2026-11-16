/**
 * MollenOS
 *
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
 * Shared Memory Access System Calls
 */

#include <os/types/syscall.h>
#include <ddk/barrier.h>
#include <debug.h>
#include <assert.h>
#include <handles/handle.h>
#include <heap.h>
#include <memoryspace.h>
#include <shm/access.h>
#include <shm/record.h>

static oserr_t
__ReleaseObject(
    _In_ SHMRecord_t* record,
    _In_ oserr_t    (*operation)(SHMRecord_t*, void**))
{
    void*   object = record->Object;
    oserr_t status;

    if (object == NULL) {
        return OS_EOK;
    }

    // Clear the object pointer before calling the release operation. If the
    // thread is stopped halfway through, the record must not be left pointing
    // at a freed object. On failure we put the pointer back.
    record->Object = NULL;

    status = operation(record, &object);
    if (status != OS_EOK) {
        record->Object = object;
    }
    return status;
}

static void
__ExitRecord(
    _In_ void* resource)
{
    SHMRecord_t* record = resource;
    oserr_t      status;

    // We end up here when the last reference is dropped, or when all threads
    // in the process group (including any background worker threads) have
    // stopped. Mark the record closed first so SHMRecordBegin refuses new
    // callers, then clean up the object.
    atomic_store(&record->State, SHMRecordClosed);

    if (record->Ops->CancelOperation != NULL) {
        status = record->Ops->CancelOperation(record);
        if (status != OS_EOK) {
            ERROR("__ExitRecord: CancelOperation resource %u failed: %u", record->Handle, status);
            return;
        }
    }

    status = __ReleaseObject(record, record->Ops->Cleanup);
    if (status != OS_EOK) {
        ERROR("__ExitRecord: object cleanup for resource %u failed: %u", record->Handle, status);
    }
}

static void
__DestroyRecord(
    _In_ void* resource)
{
    SHMRecord_t* record = resource;
    oserr_t      status;

    // Grant cleanup has normally cleared the object by now. If releasing it
    // fails here, keep the record allocated so nothing frees memory that might
    // still be in use.
    if (record->Ops->CancelOperation != NULL) {
        status = record->Ops->CancelOperation(record);
        if (status != OS_EOK) {
            ERROR("__DestroyRecord: CancelOperation resource %u failed: %u", record->Handle, status);
            return;
        }
    }

    status = __ReleaseObject(record, record->Ops->Cleanup);
    if (status != OS_EOK) {
        ERROR("__DestroyRecord: object cleanup for resource %u failed: %u", record->Handle, status);
        return;
    }
    kfree(record);
}

oserr_t
SHMRecordCreate(
    _In_  const SHMRecordOps_t* ops,
    _Out_ SHMRecord_t**        recordOut)
{
    SHMRecord_t* record;

    record = kmalloc(sizeof(SHMRecord_t));
    if (record == NULL) {
        return OS_EOOM;
    }

    record->Ops = ops;
    record->Object = NULL;
    record->PendingOperation = NULL;
    atomic_init(&record->State, SHMRecordUpdating);

    record->Handle = CreateHandle(
        HandleTypeSHMAccess,
        __ExitRecord,
        __DestroyRecord,
        record
    );
    if (record->Handle == UUID_INVALID) {
        kfree(record);
        return OS_EOOM;
    }

    *recordOut = record;
    return OS_EOK;
}

oserr_t
SHMRecordBegin(
    _In_  uuid_t                handle,
    _In_  const SHMRecordOps_t* ops,
    _Out_ SHMRecord_t**         recordOut)
{
    SHMRecord_t*        record;
    enum SHMRecordState expected = SHMRecordIdle;
    oserr_t             status;

    status = AcquireHandle(handle, HandleTypeSHMAccess, (void**)&record);
    if (status != OS_EOK) {
        return status == OS_EUNKNOWN ? OS_EINVALPARAMS : status;
    }

    if (ops != NULL && record->Ops != ops) {
        uuid_t releasedHandle = handle;
        oserr_t releaseStatus = DestroyHandle(releasedHandle);

        if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE) {
            ERROR("%s: DestroyHandle %u failed: %u", __func__, releasedHandle, releaseStatus);
        }
        return OS_EINVALPARAMS;
    }

    if (!atomic_compare_exchange_strong(&record->State, &expected, SHMRecordUpdating)) {
        uuid_t  releasedHandle = handle;
        oserr_t releaseStatus = DestroyHandle(releasedHandle);

        if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE) {
            ERROR("%s: DestroyHandle %u failed: %u", __func__, releasedHandle, releaseStatus);
        }
        return expected == SHMRecordClosed ? OS_ENOENT : OS_EBUSY;
    }

    *recordOut = record;
    return OS_EOK;
}

void
SHMRecordEnd(
    _In_ SHMRecord_t* record)
{
    uuid_t  handle = record->Handle;
    uuid_t  releasedHandle;
    oserr_t releaseStatus;

    assert(record->PendingOperation == NULL);
    atomic_store(&record->State, SHMRecordIdle);

    releasedHandle = handle;
    releaseStatus = DestroyHandle(releasedHandle);
    if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE) {
        ERROR("%s: DestroyHandle %u failed: %u", __func__, releasedHandle, releaseStatus);
    }
}

oserr_t
SHMRecordRelease(
    _In_ SHMRecord_t* record)
{
    return __ReleaseObject(record, record->Ops->Release);
}

oserr_t
SHMRecordCleanup(
    _In_ SHMRecord_t* record)
{
    // Rollback after a failed setup runs on the thread that is still inside the
    // call, so no operation of the record's own can be pending. Only shutdown
    // may cancel a pending operation, and only after all threads of the caller
    // have stopped.
    assert(atomic_load(&record->State) == SHMRecordUpdating);
    assert(record->PendingOperation == NULL);
    return __ReleaseObject(record, record->Ops->Cleanup);
}

void
SHMRecordRemove(
    _In_ SHMRecord_t* record)
{
    uuid_t  releasedHandle;
    oserr_t releaseStatus;

    atomic_store(&record->State, SHMRecordClosed);

    releasedHandle = record->Handle;
    releaseStatus = DestroyHandle(releasedHandle);
    if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE) {
        ERROR("%s: DestroyHandle %u failed: %u", __func__, releasedHandle, releaseStatus);
    }
}

static oserr_t
__ReleaseCpu(
    _In_    SHMRecord_t* record,
    _InOut_ void**       object)
{
    struct SHMAccess* access = *object;
    oserr_t           status;
    (void)record;

    status = SHMAccessDestroy(&access);
    *object = access;
    return status;
}

static oserr_t
__CleanupCpu(
    _In_    SHMRecord_t* record,
    _InOut_ void**       object)
{
    dma_mb();
    SHMAccessEnd(*object);
    return __ReleaseCpu(record, object);
}

static oserr_t
__EndCpu(
    _In_ SHMRecord_t*                         session,
    _In_ const struct SHMAccessEndParameters* parameters)
{
    struct SHMAccess* access = session->Object;

    if (parameters->Kind != SHMAccessCpu || parameters->Length != 0) {
        return OS_EINVALPARAMS;
    }

    if (access->Phase != SHMAccessActive || access->Holder != SHMAccessHolderCpu) {
        return OS_EINVALPARAMS;
    }
    dma_mb();

    SHMAccessEnd(access);
    return OS_EOK;
}

static oserr_t
__BeginCpu(
    _In_ SHMRecord_t*                           session,
    _In_ const struct SHMAccessBeginParameters* parameters,
    _In_ OSAsyncContext_t*                      async)
{
    struct SHMAccessWait wait = {
        .Blocking = parameters->Mode == SHMWaitUntilAvailable,
        .Async = async,
        .Deadline = &parameters->Deadline
    };
    struct SHMAccess* access = session->Object;
    oserr_t           status;

    if (parameters->Kind != SHMAccessCpu || parameters->Offset != 0 || parameters->Length != 0) {
        return OS_EINVALPARAMS;
    }

    status = SHMAccessBegin(
        access,
        0,
        access->Length,
        SHMAccessHolderCpu,
        parameters->Write,
        &wait
    );
    if (status == OS_EOK) {
        // Device writes were already synchronized when the device access
        // completed, so a barrier is all we need. Do not invalidate the cache
        // here, as other readers may share those lines and still need data
        // written by the CPU.
        dma_mb();
    }
    return status;
}

oserr_t
ScShmAccessBegin(
    _In_ OSAsyncContext_t*                      async,
    _In_ uuid_t                                 handle,
    _In_ const struct SHMAccessBeginParameters* userParameters)
{
    struct SHMAccessBeginParameters parameters;
    SHMRecord_t*                    record;
    oserr_t                         status;

    status = MemorySpaceCopyFromUser(
        &parameters,
        userParameters,
        sizeof(struct SHMAccessBeginParameters)
    );
    if (status != OS_EOK) {
        return status;
    }

    if (parameters.Mode != SHMWaitNonBlocking && parameters.Mode != SHMWaitUntilAvailable) {
        return OS_EINVALPARAMS;
    }

    if (parameters.Mode == SHMWaitUntilAvailable) {
        if (parameters.Deadline.Seconds < 0 || parameters.Deadline.Nanoseconds < 0 ||
            parameters.Deadline.Nanoseconds >= 1000000000) {
            return OS_EINVALPARAMS;
        }
    }

    status = SHMRecordBegin(handle, NULL, &record);
    if (status != OS_EOK) {
        return status;
    }

    if (record->Ops->Begin != NULL) {
        status = record->Ops->Begin(record, &parameters, async);
    } else {
        status = OS_EINVALPARAMS;
    }

    // If the wait moved to a background thread (OS_EFORKED), leave the record
    // pinned in Updating so that thread can finish and clean it up.
    if (status != OS_EFORKED) {
        if (status != OS_EOK && parameters.Mode == SHMWaitNonBlocking) {
            WARNING("ScShmAccessBegin: handle %u kind %u failed: %u",
                    handle, parameters.Kind, status);
        }
        SHMRecordEnd(record);
    }
    return status;
}

oserr_t
ScShmAccessEnd(
    _In_ uuid_t             handle,
    _In_ enum SHMAccessKind kind,
    _In_ size_t             completedLength)
{
    struct SHMAccessEndParameters parameters = {
        .Kind = kind,
        .Length = completedLength
    };
    SHMRecord_t* record;
    oserr_t      status;

    status = SHMRecordBegin(handle, NULL, &record);
    if (status != OS_EOK) {
        return status;
    }

    if (record->Ops->End != NULL) {
        status = record->Ops->End(record, &parameters);
    } else {
        status = OS_EINVALPARAMS;
    }
    if (status != OS_EOK) {
        WARNING("ScShmAccessEnd: handle %u kind %u failed: %u", handle, kind, status);
    }
    SHMRecordEnd(record);
    return status;
}

static const SHMRecordOps_t g_cpuOps = {
    .Release = __ReleaseCpu,
    .Cleanup = __CleanupCpu,
    .Begin = __BeginCpu,
    .End = __EndCpu
};

oserr_t
ScShmAccessCreate(
    _In_  uuid_t  shm,
    _In_  size_t  offset,
    _In_  size_t  length,
    _Out_ uuid_t* handleOut)
{
    SHMRecord_t*      record;
    struct SHMAccess* access = NULL;
    oserr_t           status;

    status = SHMRecordCreate(&g_cpuOps, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMAccessCreate(shm, offset, length, &access);
    if (status != OS_EOK) {
        SHMRecordRemove(record);
        return status;
    }

    record->Object = access;

    status = MemorySpaceCopyToUser(
        handleOut,
        &record->Handle,
        sizeof(record->Handle)
    );
    if (status != OS_EOK) {
        SHMRecordRemove(record);
        return status;
    }
    atomic_store(&record->State, SHMRecordIdle);
    return OS_EOK;
}

oserr_t
ScResourceRelease(
    _In_ uuid_t handle)
{
    SHMRecord_t* record;
    oserr_t      status;
    uuid_t       releasedHandle;
    oserr_t      releaseStatus;

    TRACE("ScResourceRelease(handle=%u)", handle);

    status = SHMRecordBegin(handle, NULL, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMRecordRelease(record);
    if (status != OS_EOK) {
        WARNING(
            "ScResourceRelease: resource %u failed: %u; handle remains available",
            handle,
            status
        );
        SHMRecordEnd(record);
        return status;
    }

    SHMRecordRemove(record);

    // SHMRecordRemove dropped the initial reference. Now drop the one we got
    // from SHMRecordBegin.
    releasedHandle = handle;
    releaseStatus = DestroyHandle(releasedHandle);
    if (releaseStatus != OS_EOK && releaseStatus != OS_EINCOMPLETE) {
        ERROR("%s: DestroyHandle %u failed: %u", __func__, releasedHandle, releaseStatus);
    }

    TRACE("ScResourceRelease: handle %u released", handle);
    return OS_EOK;
}
