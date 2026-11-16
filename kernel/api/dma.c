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
 * DMA System Calls
 */

//#define __TRACE

#include <os/types/syscall.h>
#include <capabilities.h>
#include <debug.h>
#include <shm/record.h>
#include <memoryspace.h>
#include <shm/shm.h>
#include <shm/device.h>
#include <string.h>

static oserr_t
__CleanupContext(
    _In_    SHMRecord_t* record,
    _InOut_ void**       object)
{
    struct SHMDeviceContext* context = *object;
    (void)record;

    SHMDeviceContextAbandon(&context);
    *object = context;
    return OS_EOK;
}

static oserr_t
__ReleaseContext(
    _In_    SHMRecord_t* record,
    _InOut_ void**       object)
{
    struct SHMDeviceContext* context = *object;
    oserr_t                  status;
    (void)record;

    status = SHMDeviceContextClose(&context);
    *object = context;
    return status;
}

static oserr_t
__CleanupMapping(
    _In_    SHMRecord_t* record,
    _InOut_ void**       object)
{
    struct SHMDeviceMapping* mapping = *object;
    oserr_t                  status;
    (void)record;

    status = SHMDeviceAbandon(&mapping);
    *object = mapping;
    return status;
}

static oserr_t
__UnmapMapping(
    _In_    SHMRecord_t* record,
    _InOut_ void**       object)
{
    SHMDeviceMapping_t*   mapping = *object;
    SHMDeviceOperation_t* operation = NULL;
    oserr_t               status;

    status = SHMDeviceMappingBeginOperation(record, mapping, SHMDeviceOperationRelease, &operation);
    if (status != OS_EOK) {
        ERROR("__UnmapMapping: SHMDeviceMappingBeginOperation resource %u failed: %u", record->Handle, status);
        return status;
    }

    status = SHMDeviceUnmap(operation, &mapping);
    if (status != OS_EOK) {
        ERROR("__UnmapMapping: SHMDeviceUnmap resource %u failed: %u", record->Handle, status);
    }

    *object = mapping;

    // SHMDeviceUnmap can drop the mapping's initial reference, so the operation
    // token keeps the mapping struct alive until SHMDeviceOperationEnd returns.
    SHMDeviceOperationEnd(&operation);
    return status;
}

static oserr_t
__BeginMappingAccess(
    _In_ SHMRecord_t*                           record,
    _In_ const struct SHMAccessBeginParameters* parameters,
    _In_ OSAsyncContext_t*                      async)
{
    SHMDeviceOperation_t* operation = NULL;
    oserr_t               status;

    struct SHMAccessWait wait = {
        .Blocking = parameters->Mode == SHMWaitUntilAvailable,
        .Async = async,
        .Deadline = &parameters->Deadline
    };

    if (parameters->Write) {
        return OS_EINVALPARAMS;
    }

    status = SHMDeviceMappingBeginOperation(
        record,
        record->Object,
        SHMDeviceOperationSubmit,
        &operation
    );
    if (status != OS_EOK) {
        return status;
    }

    switch (parameters->Kind) {
        case DMAAccessTransfer:
            status = SHMDeviceSyncForDevice(
                operation,
                record->Object,
                parameters->Offset,
                parameters->Length,
                &wait
            );
            if (status == OS_EFORKED) {
                goto exit;
            } else if (status != OS_EOK) {
                ERROR("__BeginMappingAccess: SHMDeviceSyncForDevice resource %u failed: %u", record->Handle, status);
                goto exit;
            }
            break;
        case DMAAccessUse:
            if (parameters->Offset != 0 || parameters->Length != 0) {
                status = OS_EINVALPARAMS;
                goto exit;
            }

            status = SHMDeviceBeginUse(operation, record->Object, &wait);
            if (status == OS_EFORKED) {
                goto exit;
            } else if (status != OS_EOK) {
                ERROR("__BeginMappingAccess: SHMDeviceBeginUse resource %u failed: %u", record->Handle, status);
                goto exit;
            }
            break;
        default:
            status = OS_EINVALPARAMS;
            goto exit;
    }

    status = SHMDeviceOperationCheck(operation);
    if (status != OS_EOK) {
        ERROR("__BeginMappingAccess: SHMDeviceOperationCheck resource %u failed: %u", record->Handle, status);
    }

exit:
    if (status != OS_EFORKED) {
        SHMDeviceOperationEnd(&operation);
    }
    return status;
}

static oserr_t
__EndMappingAccess(
    _In_ SHMRecord_t*                         record,
    _In_ const struct SHMAccessEndParameters* parameters)
{
    SHMDeviceOperation_t* operation = NULL;
    oserr_t               status;

    status = SHMDeviceMappingBeginOperation(
        record,
        record->Object,
        SHMDeviceOperationComplete,
        &operation
    );
    if (status != OS_EOK) {
        return status;
    }

    switch (parameters->Kind) {
        case DMAAccessTransfer:
            status = SHMDeviceSyncForCpu(
                operation,
                record->Object,
                parameters->Length
            );
            if (status != OS_EOK) {
                ERROR("__EndMappingAccess: SHMDeviceSyncForCpu resource %u failed: %u", record->Handle, status);
            }
            break;
        case DMAAccessUse:
            if (parameters->Length != 0) {
                status = OS_EINVALPARAMS;
                break;
            }

            status = SHMDeviceEndUse(operation, record->Object);
            if (status != OS_EOK) {
                ERROR("__EndMappingAccess: SHMDeviceEndUse resource %u failed: %u", record->Handle, status);
            }
            break;
        default:
            status = OS_EINVALPARAMS;
            break;
    }

    // If the binding closes while this call is running, the caller already
    // confirmed the hardware finished this transfer. Stop confirmation waits for
    // this operation to finish before releasing memory.
    SHMDeviceOperationEnd(&operation);
    return status;
}

static const SHMRecordOps_t g_contextOps = {
    .Release = __ReleaseContext,
    .Cleanup = __CleanupContext,
    .CancelOperation = SHMDeviceOperationCancel
};

static const SHMRecordOps_t g_mappingOps = {
    .Release = __UnmapMapping,
    .Cleanup = __CleanupMapping,
    .Begin = __BeginMappingAccess,
    .End = __EndMappingAccess,
    .CancelOperation = SHMDeviceOperationCancel
};

oserr_t
ScDmaOpenBound(
    _In_  uuid_t                            binding,
    _In_  const OSDmaBoundOpenParameters_t* userParameters,
    _Out_ uuid_t*                           contextOut)
{
    OSDmaBoundOpenParameters_t parameters;
    SHMRecord_t*               record;
    SHMDeviceContext_t*        context = NULL;
    oserr_t                    status;

    // The binding handle carries the device's registered rules, so drivers cannot
    // swap in their own ranges or cache policy, or raise the address limit.
    status = MemorySpaceCopyFromUser(&parameters, userParameters, sizeof(parameters));
    if (status != OS_EOK) {
        ERROR("ScDmaOpenBound: MemorySpaceCopyFromUser binding %u failed: %u", binding, status);
        return status;
    }

    status = SHMRecordCreate(&g_contextOps, &record);
    if (status != OS_EOK) {
        ERROR("ScDmaOpenBound: SHMRecordCreate binding %u failed: %u", binding, status);
        return status;
    }

    status = SHMDeviceContextPrepareBound(binding, &parameters, &context);
    if (status != OS_EOK) {
        ERROR("ScDmaOpenBound: SHMDeviceContextPrepareBound binding %u failed: %u", binding, status);
        goto error;
    }

    record->Object = context;
    status = MemorySpaceCopyToUser(contextOut, &record->Handle, sizeof(record->Handle));
    if (status != OS_EOK) {
        ERROR("ScDmaOpenBound: MemorySpaceCopyToUser binding %u failed: %u", binding, status);
        goto error;
    }

    status = SHMDeviceContextPublishBound(context);
    if (status != OS_EOK) {
        ERROR("ScDmaOpenBound: SHMDeviceContextPublishBound binding %u failed: %u", binding, status);
        goto error;
    }

    // The context is now published under the group lock, and setting the record
    // to Idle cannot fail. If the binding closes right here, cleanup will still
    // see a valid context and cancel it cleanly.
    atomic_store(&record->State, SHMRecordIdle);
    return OS_EOK;

error:
    record->Object = NULL;
    SHMDeviceContextCancelBound(&context);
    SHMRecordRemove(record);
    return status;
}

static void
__MappingResult(
    _In_  SHMDeviceOperation_t*      operation,
    _In_  SHMRecord_t*               record,
    _Out_ struct OSDmaMappingResult* result)
{
    const struct DMASegment* segments;

    result->Handle = record->Handle;
    segments = SHMDeviceMappingSegments(operation, record->Object, &result->SegmentCount);
    memset(&result->InlineSegment, 0, sizeof(result->InlineSegment));
    if (result->SegmentCount == 1) {
        result->InlineSegment = segments[0];
    }
}

static oserr_t
__CreateMappingRecord(
    _In_  uuid_t        context,
    _Out_ SHMRecord_t** parentOut,
    _Out_ SHMRecord_t** recordOut)
{
    SHMRecord_t*          parent;
    SHMRecord_t*          record;
    SHMDeviceOperation_t* operation = NULL;
    oserr_t               status;

    status = SHMRecordBegin(context, &g_contextOps, &parent);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMDeviceContextBeginOperation(parent, parent->Object, SHMDeviceOperationSubmit, &operation);
    if (status != OS_EOK) {
        SHMRecordEnd(parent);
        return status;
    }

    status = SHMRecordCreate(&g_mappingOps, &record);
    if (status != OS_EOK) {
        ERROR("__CreateMappingRecord: SHMRecordCreate context %u failed: %u", context, status);
        SHMDeviceOperationEnd(&operation);
        SHMRecordEnd(parent);
        return status;
    }

    *parentOut = parent;
    *recordOut = record;
    return OS_EOK;
}

static oserr_t
__PublishMapping(
    _In_  SHMRecord_t* parent,
    _In_  SHMRecord_t* record,
    _In_  const void*  result,
    _In_  size_t       size,
    _Out_ void*        userOut)
{
    SHMDeviceOperation_t* operation = parent->PendingOperation;
    oserr_t               status;

    status = MemorySpaceCopyToUser(userOut, result, size);
    if (status != OS_EOK) {
        ERROR("__PublishMapping: MemorySpaceCopyToUser mapping %u result failed: %u",
              record->Handle, status);
        return status;
    }

    status = SHMDeviceOperationCheck(operation);
    if (status != OS_EOK) {
        return status;
    }

    // Trace before we publish the mapping. Once it is idle, another syscall
    // may release it at any time.
    TRACE("__PublishMapping: publishing mapping %u, context %u",
          record->Handle, parent->Handle);
    atomic_store(&record->State, SHMRecordIdle);

    SHMDeviceOperationEnd(&operation);
    SHMRecordEnd(parent);
    return OS_EOK;
}

static void
__RollbackMapping(
    _In_ SHMRecord_t* parent,
    _In_ SHMRecord_t* record,
    _In_ SHMHandle_t* buffer,
    _In_ oserr_t      status)
{
    SHMDeviceOperation_t* operation = parent->PendingOperation;
    oserr_t               cleanupStatus;

    // Try releasing normally while the binding is live. If the binding closed
    // in the meantime, SHMRecordRemove hands the mapping over to stop cleanup.
    // Keep the parent context operation open across both steps and return the
    // original error.
    cleanupStatus = SHMRecordRelease(record);
    if (cleanupStatus != OS_EOK) {
        ERROR("__RollbackMapping: SHMDeviceUnmap mapping %u failed: %u (original status %u)",
              record->Handle, cleanupStatus, status);
    }

    if (buffer != NULL && buffer->ID != UUID_INVALID) {
        cleanupStatus = SHMDetach(buffer);
        if (cleanupStatus != OS_EOK) {
            ERROR("__RollbackMapping: SHMDetach buffer %u failed: %u (original status %u)",
                  buffer->ID, cleanupStatus, status);
        }
    }

    SHMRecordRemove(record);
    SHMDeviceOperationEnd(&operation);
    SHMRecordEnd(parent);
}

oserr_t
ScDmaAllocate(
    _In_  uuid_t                        context,
    _In_  const struct DMARequirements* userRequirements,
    _Out_ struct OSDmaAllocationResult* resultOut)
{
    struct DMARequirements       requirements;
    struct OSDmaAllocationResult result = { 0 };
    SHMRecord_t*                 parent;
    SHMRecord_t*                 record;
    struct SHMDeviceMapping*     mapping = NULL;
    oserr_t                      status;

    TRACE("ScDmaAllocate(context=%u)", context);

    status = MemorySpaceCopyFromUser(
        &requirements,
        userRequirements,
        sizeof(struct DMARequirements)
    );
    if (status != OS_EOK) {
        ERROR("ScDmaAllocate: MemorySpaceCopyFromUser requirements for context %u failed: %u", context, status);
        return status;
    }

    status = __CreateMappingRecord(context, &parent, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMDeviceAllocate(
        parent->PendingOperation,
        parent->Object,
        &requirements,
        &result.Buffer,
        &mapping
    );
    if (status != OS_EOK) {
        ERROR("ScDmaAllocate: SHMDeviceAllocate context %u, length %zu failed: %u",
              context, requirements.Length, status);
        goto error;
    }
    record->Object = mapping;

    __MappingResult(parent->PendingOperation, record, &result.Mapping);
    status = __PublishMapping(parent, record, &result, sizeof(result), resultOut);
    if (status == OS_EOK) {
        return status;
    }

error:
    __RollbackMapping(parent, record, &result.Buffer, status);
    return status;
}

oserr_t
ScDmaMapShm(
    _In_  uuid_t                           context,
    _In_  uuid_t                           shm,
    _In_  const struct OSDmaMapParameters* userParameters,
    _Out_ struct OSDmaMappingResult*       resultOut)
{
    struct OSDmaMapParameters parameters;
    struct OSDmaMappingResult result;
    SHMRecord_t*              parent;
    SHMRecord_t*              record;
    struct SHMDeviceMapping*  mapping = NULL;
    enum SHMDeviceMappingKind kind;
    oserr_t                   status;

    TRACE("ScDmaMapShm(context=%u, shm=%u)", context, shm);

    status = MemorySpaceCopyFromUser(&parameters, userParameters, sizeof(parameters));
    if (status != OS_EOK) {
        ERROR("ScDmaMapShm: MemorySpaceCopyFromUser parameters for context %u, SHM %u failed: %u",
              context, shm, status);
        return status;
    }

    switch (parameters.Flags) {
        case DMA_MAP_REQUIRE_DIRECT | DMA_MAP_PERSISTENT:
            kind = SHMDeviceMappingPersistent;
            break;
        case DMA_MAP_REQUIRE_DIRECT:
            kind = SHMDeviceMappingStreamingDirect;
            break;
        case 0:
            kind = SHMDeviceMappingStreamingBounce;
            break;
        default:
            return OS_EINVALPARAMS;
    }

    status = __CreateMappingRecord(context, &parent, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMDeviceMap(
        parent->PendingOperation,
        parent->Object,
        shm,
        parameters.Offset,
        parameters.Length,
        parameters.Direction,
        kind,
        &parameters.Requirements,
        &mapping
    );
    if (status != OS_EOK) {
        ERROR("ScDmaMapShm: SHMDeviceMap context %u, SHM %u, offset %zu, length %zu, direction %u failed: %u",
              context, shm, parameters.Offset, parameters.Length, parameters.Direction, status);
        goto error;
    }
    record->Object = mapping;

    __MappingResult(parent->PendingOperation, record, &result);
    status = __PublishMapping(parent, record, &result, sizeof(result), resultOut);
    if (status == OS_EOK) {
        return status;
    }

error:
    __RollbackMapping(parent, record, NULL, status);
    return status;
}

oserr_t
ScDmaGetSegments(
    _In_  uuid_t             mapping,
    _Out_ struct DMASegment* entries,
    _In_  uint32_t           capacity,
    _Out_ uint32_t*          countOut)
{
    SHMRecord_t*             record;
    SHMDeviceOperation_t*    operation = NULL;
    const struct DMASegment* segments;
    uint32_t                 count;
    oserr_t                  status;

    TRACE("ScDmaGetSegments(mapping=%u, capacity=%u, entries=%p)", mapping, capacity, entries);
    status = SHMRecordBegin(mapping, &g_mappingOps, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMDeviceMappingBeginOperation(
        record,
        record->Object,
        SHMDeviceOperationSubmit,
        &operation
    );
    if (status != OS_EOK) {
        goto exit;
    }

    segments = SHMDeviceMappingSegments(
        operation,
        record->Object,
        &count
    );
    if (segments == NULL) {
        // An earlier release may have stopped halfway through removing the
        // device addresses. In this case we keep the handle so the cleanup can
        // be retried, but the segments must not be handed out again.
        status = OS_EBUSY;
        goto exit;
    }

    status = MemorySpaceCopyToUser(countOut, &count, sizeof(count));
    if (status != OS_EOK) {
        ERROR("ScDmaGetSegments: MemorySpaceCopyToUser count for mapping %u failed: %u", mapping, status);
        goto exit;
    }

    if (entries == NULL) {
        goto exit;
    }

    if (capacity < count) {
        TRACE("ScDmaGetSegments: mapping %u needs %u segments, capacity %u", mapping, count, capacity);
        status = OS_EBUFFER;
        goto exit;
    }

    status = MemorySpaceCopyToUser(entries, segments, count * sizeof(*segments));
    if (status != OS_EOK) {
        ERROR("ScDmaGetSegments: MemorySpaceCopyToUser %u segments for mapping %u failed: %u", count, mapping, status);
        goto exit;
    }

exit:
    if (status == OS_EOK) {
        status = SHMDeviceOperationCheck(operation);
    }
    SHMDeviceOperationEnd(&operation);
    SHMRecordEnd(record);
    return status;
}
