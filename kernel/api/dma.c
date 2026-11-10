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
    _In_    struct SHMRecord* record,
    _InOut_ void**            object)
{
    struct SHMDeviceContext* context = *object;
    (void)record;

    SHMDeviceContextRelease(&context);
    *object = context;
    return OS_EOK;
}

static oserr_t
__ReleaseContext(
    _In_    struct SHMRecord* record,
    _InOut_ void**            object)
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
    _In_    struct SHMRecord* record,
    _InOut_ void**            object)
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
    _In_    struct SHMRecord* record,
    _InOut_ void**            object)
{
    struct SHMDeviceMapping* mapping = *object;
    oserr_t                  status;
    (void)record;

    status = SHMDeviceUnmap(&mapping);
    *object = mapping;
    return status;
}

static oserr_t
__BeginMappingAccess(
    _In_ struct SHMRecord*                      record,
    _In_ const struct SHMAccessBeginParameters* parameters,
    _In_ OSAsyncContext_t*                      async)
{
    struct SHMAccessWait wait = {
        .Blocking = parameters->Mode == SHMWaitUntilAvailable,
        .Async = async,
        .Deadline = &parameters->Deadline
    };

    if (parameters->Write) {
        return OS_EINVALPARAMS;
    }

    switch (parameters->Kind) {
        case DMAAccessTransfer:
            return SHMDeviceSyncForDevice(
                record->Object,
                parameters->Offset,
                parameters->Length,
                &wait
            );
        case DMAAccessUse:
            if (parameters->Offset != 0 || parameters->Length != 0) {
                return OS_EINVALPARAMS;
            }
            return SHMDeviceBeginUse(record->Object, &wait);
        default:
            return OS_EINVALPARAMS;
    }
}

static oserr_t
__EndMappingAccess(
    _In_ struct SHMRecord*                    record,
    _In_ const struct SHMAccessEndParameters* parameters)
{
    switch (parameters->Kind) {
        case DMAAccessTransfer:
            return SHMDeviceSyncForCpu(
                record->Object,
                parameters->Length
            );
        case DMAAccessUse:
            if (parameters->Length != 0) {
                return OS_EINVALPARAMS;
            }
            return SHMDeviceEndUse(record->Object);
        default:
            return OS_EINVALPARAMS;
    }
}

static const struct SHMRecordOps g_contextOps = {
    .Release = __ReleaseContext,
    .Abandon = __CleanupContext
};

static const struct SHMRecordOps g_mappingOps = {
    .Release = __UnmapMapping,
    .Abandon = __CleanupMapping,
    .Begin = __BeginMappingAccess,
    .End = __EndMappingAccess
};

oserr_t
ScDmaOpen(
    _In_  uuid_t                            device,
    _In_  const struct OSDmaOpenParameters* userParameters,
    _Out_ uuid_t*                           contextOut)
{
    struct OSDmaOpenParameters parameters;
    struct SHMRecord*          record;
    struct SHMDeviceContext*   context = NULL;
    oserr_t                    status;

    TRACE("ScDmaOpen(device=%u)", device);

    status = CapabilityVerify(CAPABILITY_DEVICEIO_ACCESS);
    if (status != OS_EOK) {
        WARNING("ScDmaOpen: CapabilityVerify device %u failed: %u", device, status);
        return status;
    }

    if (device == UUID_INVALID) {
        WARNING("ScDmaOpen: invalid device ID");
        return OS_EINVALPARAMS;
    }

    status = MemorySpaceCopyFromUser(
        &parameters,
        userParameters,
        sizeof(struct OSDmaOpenParameters)
    );
    if (status != OS_EOK) {
        ERROR("ScDmaOpen: MemorySpaceCopyFromUser parameters for device %u failed: %u", device, status);
        return status;
    }

    status = SHMRecordCreate(&g_contextOps, &record);
    if (status != OS_EOK) {
        ERROR("ScDmaOpen: SHMRecordCreate device %u failed: %u", device, status);
        return status;
    }

    status = SHMDeviceContextCreate(
        parameters.Configuration.Ranges,
        parameters.Configuration.Count,
        parameters.Configuration.CachePolicy,
        parameters.AddressLimit,
        &context
    );
    if (status != OS_EOK) {
        ERROR("ScDmaOpen: SHMDeviceContextCreate device %u, ranges %u, cache policy %u failed: %u",
              device, parameters.Configuration.Count, parameters.Configuration.CachePolicy, status);
        goto error;
    }

    record->Object = context;

    status = MemorySpaceCopyToUser(
        contextOut,
        &record->Handle,
        sizeof(record->Handle)
    );
    if (status != OS_EOK) {
        ERROR("ScDmaOpen: MemorySpaceCopyToUser context %u result failed: %u", record->Handle, status);
        goto error;
    }

    // Trace before we publish the record. Once it is visible, another syscall
    // may close it at any time.
    TRACE("ScDmaOpen: publishing context %u for device %u", record->Handle, device);
    atomic_store(&record->State, SHMRecordIdle);
    return OS_EOK;

error:
    SHMRecordRelease(record);
    SHMRecordRemove(record);
    return status;
}

static void
__MappingResult(
    _In_  struct SHMRecord*          record,
    _Out_ struct OSDmaMappingResult* result)
{
    const struct DMASegment* segments;

    result->Handle = record->Handle;
    segments = SHMDeviceMappingSegments(record->Object, &result->SegmentCount);
    memset(&result->InlineSegment, 0, sizeof(result->InlineSegment));
    if (result->SegmentCount == 1) {
        result->InlineSegment = segments[0];
    }
}

static oserr_t
__CreateMappingRecord(
    _In_  uuid_t             context,
    _Out_ struct SHMRecord** parentOut,
    _Out_ struct SHMRecord** recordOut)
{
    struct SHMRecord* parent;
    struct SHMRecord* record;
    oserr_t           status;

    status = SHMRecordBegin(context, &g_contextOps, &parent);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMRecordCreate(&g_mappingOps, &record);
    if (status != OS_EOK) {
        ERROR("__CreateMappingRecord: SHMRecordCreate context %u failed: %u", context, status);
        SHMRecordEnd(parent);
        return status;
    }

    *parentOut = parent;
    *recordOut = record;
    return OS_EOK;
}

static oserr_t
__PublishMapping(
    _In_  struct SHMRecord* parent,
    _In_  struct SHMRecord* record,
    _In_  const void*      result,
    _In_  size_t           size,
    _Out_ void*            userOut)
{
    oserr_t status;

    status = MemorySpaceCopyToUser(userOut, result, size);
    if (status != OS_EOK) {
        ERROR("__PublishMapping: MemorySpaceCopyToUser mapping %u result failed: %u",
              record->Handle, status);
        return status;
    }

    // Trace before we publish the mapping. Once it is idle, another syscall
    // may release it at any time.
    TRACE("__PublishMapping: publishing mapping %u, context %u",
          record->Handle, parent->Handle);
    atomic_store(&record->State, SHMRecordIdle);
    SHMRecordEnd(parent);
    return OS_EOK;
}

static void
__RollbackMapping(
    _In_ struct SHMRecord* parent,
    _In_ struct SHMRecord* record,
    _In_ SHMHandle_t*      buffer,
    _In_ oserr_t           status)
{
    oserr_t cleanupStatus;

    // The mapping was never published, so no device can be using it yet. A
    // normal release (unmap) is enough here.
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
    struct SHMRecord*            parent;
    struct SHMRecord*            record;
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

    status = SHMDeviceAllocate(parent->Object, &requirements, &result.Buffer, &mapping);
    if (status != OS_EOK) {
        ERROR("ScDmaAllocate: SHMDeviceAllocate context %u, length %zu failed: %u",
              context, requirements.Length, status);
        goto error;
    }
    record->Object = mapping;

    __MappingResult(record, &result.Mapping);
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
    struct SHMRecord*         parent;
    struct SHMRecord*         record;
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

    __MappingResult(record, &result);
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
    struct SHMRecord*        record;
    const struct DMASegment* segments;
    uint32_t                 count;
    oserr_t                  status;

    TRACE("ScDmaGetSegments(mapping=%u, capacity=%u, entries=%p)", mapping, capacity, entries);
    status = SHMRecordBegin(mapping, &g_mappingOps, &record);
    if (status != OS_EOK) {
        return status;
    }

    segments = SHMDeviceMappingSegments(record->Object, &count);
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
    SHMRecordEnd(record);
    return status;
}
