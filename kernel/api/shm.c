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
    _In_ struct SHMRecord* record,
    _In_ oserr_t         (*operation)(struct SHMRecord*, void**))
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
    struct SHMRecord* record = resource;
    oserr_t           status;

    // We end up here when the last reference is dropped, or when all threads
    // in the group and any async children have stopped. Mark the record closed
    // first, so SHMRecordBegin refuses it, then abandon the object.
    atomic_store(&record->State, SHMRecordClosed);
    status = __ReleaseObject(record, record->Ops->Abandon);
    if (status != OS_EOK) {
        ERROR("SHM: abandoning resource %u failed: %u", record->Handle, status);
    }
}

static void
__DestroyRecord(
    _In_ void* resource)
{
    struct SHMRecord* record = resource;
    oserr_t           status;

    // Grant cleanup has normally cleared the object by now. If releasing it
    // fails here, we keep the record around instead of freeing it.
    status = __ReleaseObject(record, record->Ops->Abandon);
    if (status != OS_EOK) {
        ERROR("SHM: final cleanup of resource %u failed: %u", record->Handle, status);
        return;
    }
    kfree(record);
}

oserr_t
SHMRecordCreate(
    _In_  const struct SHMRecordOps* ops,
    _Out_ struct SHMRecord**        recordOut)
{
    struct SHMRecord* record;

    record = kmalloc(sizeof(struct SHMRecord));
    if (record == NULL) {
        return OS_EOOM;
    }

    record->Ops = ops;
    record->Object = NULL;
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
    _In_  uuid_t                     handle,
    _In_  const struct SHMRecordOps* ops,
    _Out_ struct SHMRecord**         recordOut)
{
    struct SHMRecord*   record;
    enum SHMRecordState expected = SHMRecordIdle;
    oserr_t             status;

    status = AcquireHandle(handle, HandleTypeSHMAccess, (void**)&record);
    if (status != OS_EOK) {
        return status == OS_EUNKNOWN ? OS_EINVALPARAMS : status;
    }

    if (ops != NULL && record->Ops != ops) {
        DestroyHandle(handle);
        return OS_EINVALPARAMS;
    }

    if (!atomic_compare_exchange_strong(&record->State, &expected, SHMRecordUpdating)) {
        DestroyHandle(handle);
        return expected == SHMRecordClosed ? OS_ENOENT : OS_EBUSY;
    }

    *recordOut = record;
    return OS_EOK;
}

void
SHMRecordEnd(
    _In_ struct SHMRecord* record)
{
    uuid_t handle = record->Handle;

    atomic_store(&record->State, SHMRecordIdle);
    DestroyHandle(handle);
}

oserr_t
SHMRecordRelease(
    _In_ struct SHMRecord* record)
{
    return __ReleaseObject(record, record->Ops->Release);
}

void
SHMRecordRemove(
    _In_ struct SHMRecord* record)
{
    atomic_store(&record->State, SHMRecordClosed);
    DestroyHandle(record->Handle);
}

static oserr_t
__ReleaseCpu(
    _In_    struct SHMRecord* record,
    _InOut_ void**           object)
{
    struct SHMAccess* access = *object;
    oserr_t           status;
    (void)record;

    status = SHMAccessDestroy(&access);
    *object = access;
    return status;
}

static oserr_t
__AbandonCpu(
    _In_    struct SHMRecord* record,
    _InOut_ void**           object)
{
    dma_mb();
    SHMAccessEnd(*object);
    return __ReleaseCpu(record, object);
}

static oserr_t
__EndCpu(
    _In_ struct SHMRecord*                 session,
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
    _In_ struct SHMRecord*                      session,
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
    struct SHMRecord*               record;
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

    // If the call was forked, the async child is still working on the record.
    // In this case we leave the record pinned until the child is done.
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
    struct SHMRecord* record;
    oserr_t           status;

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

static const struct SHMRecordOps g_cpuOps = {
    .Release = __ReleaseCpu,
    .Abandon = __AbandonCpu,
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
    struct SHMRecord* record;
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
    struct SHMRecord* record;
    oserr_t           status;

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
    DestroyHandle(handle);
    TRACE("ScResourceRelease: handle %u released", handle);
    return OS_EOK;
}
