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
#include <ds/hashtable.h>
#include <heap.h>
#include <memoryspace.h>
#include <mutex.h>
#include <shm.h>
#include <shm_device.h>
#include <string.h>
#include <limits.h>

struct __DmaRecord {
    struct __DmaRecord*       Parent;
    uuid_t                   Handle;
    uuid_t                   Owner;
    unsigned int             Mappings;
    int                      Updating;
    bool                     Published;
    struct SHMDeviceContext* Context;
    struct SHMDeviceMapping* Mapping;
};

struct __DmaEntry {
    uuid_t              Handle;
    struct __DmaRecord* Record;
};

// Registry access is confined to syscalls. Resizing may allocate and rehash,
// so its lock must allow interrupts and let contending threads sleep.
static Mutex_t     g_dmaLock = OS_MUTEX_INIT(MUTEX_FLAG_PLAIN);
static hashtable_t g_dmaRecords;
static uuid_t      g_dmaGeneration;

static uint64_t
__HandleHash(
    _In_ const void* element)
{
    const struct __DmaEntry* entry = element;

    return entry->Handle;
}

static int
__HandleCompare(
    _In_ const void* left,
    _In_ const void* right)
{
    const struct __DmaEntry* leftEntry = left;
    const struct __DmaEntry* rightEntry = right;

    return leftEntry->Handle != rightEntry->Handle;
}

static oserr_t
__Begin(
    _In_  uuid_t               handle,
    _In_  int                  mapping,
    _Out_ struct __DmaRecord** recordOut)
{
    struct __DmaEntry*  entry;
    struct __DmaRecord* record;

    MutexLock(&g_dmaLock);
    if (g_dmaRecords.capacity == 0) {
        MutexUnlock(&g_dmaLock);
        TRACE("__Begin: DMA handle %u not found", handle);
        return OS_ENOENT;
    }

    entry = hashtable_get(&g_dmaRecords, &(struct __DmaEntry) { .Handle = handle });
    if (entry == NULL || !entry->Record->Published) {
        MutexUnlock(&g_dmaLock);
        TRACE("__Begin: DMA handle %u not found or not published", handle);
        return OS_ENOENT;
    }

    record = entry->Record;
    if (record->Owner != MemorySpaceGetSecurityContext()->Handle) {
        MutexUnlock(&g_dmaLock);
        WARNING("__Begin: DMA handle %u is not owned by the caller", handle);
        return OS_EPERMISSIONS;
    }
    
    if ((record->Parent != NULL) != mapping) {
        MutexUnlock(&g_dmaLock);
        WARNING("__Begin: DMA handle %u has the wrong kind (mapping requested=%d)", handle, mapping);
        return OS_EINVALPARAMS;
    }

    if (record->Updating) {
        MutexUnlock(&g_dmaLock);
        TRACE("__Begin: DMA handle %u is already being updated", handle);
        return OS_EBUSY;
    }

    record->Updating = 1;
    *recordOut = record;
    MutexUnlock(&g_dmaLock);
    return OS_EOK;
}

static void
__End(
    _In_ struct __DmaRecord* record)
{
    MutexLock(&g_dmaLock);
    record->Updating = 0;
    MutexUnlock(&g_dmaLock);
}

static oserr_t
__Create(
    _Out_ struct __DmaRecord** recordOut)
{
    struct __DmaRecord* record = kmalloc(sizeof(*record));
    struct __DmaEntry   entry;
    oserr_t             status = OS_EOK;
    int                 result;
    const char*         failedOperation = NULL;

    if (record == NULL) {
        ERROR("__Create: DMA record allocation failed");
        return OS_EOOM;
    }
    
    memset(record, 0, sizeof(*record));
    record->Owner = MemorySpaceGetSecurityContext()->Handle;
    MutexLock(&g_dmaLock);
    
    if (g_dmaGeneration == UINT32_MAX) {
        failedOperation = "handle generation";
        status = OS_EOVERFLOW;
        goto unlock;
    }

    if (g_dmaRecords.capacity == 0) {
        result = hashtable_construct(
            &g_dmaRecords,
            HASHTABLE_MINIMUM_CAPACITY,
            sizeof(struct __DmaEntry),
            __HandleHash,
            __HandleCompare
        );
        if (result != 0) {
            failedOperation = "hashtable_construct";
            status = OS_EOOM;
            goto unlock;
        }
        g_dmaRecords.flags = HASHTABLE_FLAG_NO_SHRINK;
    }

    record->Handle = ++g_dmaGeneration;
    entry.Handle = record->Handle;
    entry.Record = record;

    // Reserve the index before creating resources or copying a handle to the
    // caller. Unpublished records cannot be acquired while setup is in flight.
    (void)hashtable_set(&g_dmaRecords, &entry);
    if (hashtable_get(&g_dmaRecords, &entry) == NULL) {
        // set returns NULL both for insertion and for failed table growth.
        failedOperation = "hashtable_set";
        status = OS_EOOM;
    }

unlock:
    MutexUnlock(&g_dmaLock);
    if (status != OS_EOK) {
        ERROR("__Create: %s failed for DMA owner %u: %u", failedOperation, record->Owner, status);
        kfree(record);
        return status;
    }
    *recordOut = record;
    TRACE("__Create: reserved unpublished DMA handle %u, owner %u", record->Handle, record->Owner);
    return OS_EOK;
}

static void
__Publish(
    _In_ struct __DmaRecord* record)
{
    MutexLock(&g_dmaLock);
    if (record->Parent != NULL) {
        record->Parent->Mappings++;
    }
    record->Published = true;
    MutexUnlock(&g_dmaLock);
}

static void
__Remove(
    _In_ struct __DmaRecord* record)
{
    hashtable_t retired = { 0 };

    MutexLock(&g_dmaLock);
    (void)hashtable_remove(&g_dmaRecords, &(struct __DmaEntry) { .Handle = record->Handle });
    if (record->Published && record->Parent != NULL) {
        record->Parent->Mappings--;
    }
    if (g_dmaRecords.element_count == 0) {
        // A later create may build a new index as soon as we unlock. This
        // detached copy exclusively owns the old storage until destruction.
        retired = g_dmaRecords;
        memset(&g_dmaRecords, 0, sizeof(g_dmaRecords));
    }
    MutexUnlock(&g_dmaLock);
    hashtable_destroy(&retired);
    kfree(record);
}

oserr_t
ScDmaOpen(
    _In_  uuid_t                            device,
    _In_  const struct OSDmaOpenParameters* userParameters,
    _Out_ uuid_t*                           contextOut)
{
    struct OSDmaOpenParameters parameters;
    struct __DmaRecord*        record;
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

    status = MemorySpaceCopyUser((void*)userParameters, &parameters, sizeof(parameters), false);
    if (status != OS_EOK) {
        ERROR("ScDmaOpen: MemorySpaceCopyUser parameters for device %u failed: %u", device, status);
        return status;
    }

    status = __Create(&record);
    if (status != OS_EOK) {
        ERROR("ScDmaOpen: __Create device %u failed: %u", device, status);
        return status;
    }

    status = SHMDeviceContextCreate(
        parameters.Configuration.Ranges,
        parameters.Configuration.Count,
        parameters.Configuration.CachePolicy,
        parameters.AddressLimit,
        &record->Context
    );
    if (status != OS_EOK) {
        ERROR("ScDmaOpen: SHMDeviceContextCreate device %u, ranges %u, cache policy %u failed: %u",
              device, parameters.Configuration.Count, parameters.Configuration.CachePolicy, status);
        goto error;
    }

    status = MemorySpaceCopyUser(contextOut, &record->Handle, sizeof(record->Handle), true);
    if (status != OS_EOK) {
        ERROR("ScDmaOpen: MemorySpaceCopyUser context %u result failed: %u", record->Handle, status);
        goto error;
    }

    // Trace before publication: another syscall may close the visible record.
    TRACE("ScDmaOpen: publishing context %u for device %u", record->Handle, device);
    __Publish(record);
    return OS_EOK;

error:
    SHMDeviceContextRelease(&record->Context);
    __Remove(record);
    return status;
}

static void
__MappingResult(
    _In_  struct __DmaRecord*        record,
    _Out_ struct OSDmaMappingResult* result)
{
    const struct DMASegment* segments;

    result->Handle = record->Handle;
    segments = SHMDeviceMappingSegments(record->Mapping, &result->SegmentCount);
    memset(&result->InlineSegment, 0, sizeof(result->InlineSegment));
    if (result->SegmentCount == 1) {
        result->InlineSegment = segments[0];
    }
}

oserr_t
ScDmaAllocate(
    _In_  uuid_t                        context,
    _In_  const struct DMARequirements* userRequirements,
    _Out_ struct OSDmaAllocationResult* resultOut)
{
    struct DMARequirements       requirements;
    struct OSDmaAllocationResult result = { 0 };
    struct __DmaRecord*          parent;
    struct __DmaRecord*          record;
    oserr_t                      status;
    oserr_t                      cleanupStatus;

    TRACE("ScDmaAllocate(context=%u)", context);
    status = MemorySpaceCopyUser((void*)userRequirements, &requirements, sizeof(requirements), false);
    if (status != OS_EOK) {
        ERROR("ScDmaAllocate: MemorySpaceCopyUser requirements for context %u failed: %u", context, status);
        return status;
    }

    status = __Begin(context, 0, &parent);
    if (status != OS_EOK) {
        return status;
    }

    status = __Create(&record);
    if (status != OS_EOK) {
        ERROR("ScDmaAllocate: __Create context %u failed: %u", context, status);
        __End(parent);
        return status;
    }

    record->Parent = parent;
    status = SHMDeviceAllocate(parent->Context, &requirements, &result.Buffer, &record->Mapping);
    if (status != OS_EOK) {
        ERROR("ScDmaAllocate: SHMDeviceAllocate context %u, length %zu failed: %u",
              context, requirements.Length, status);
        goto error;
    }

    __MappingResult(record, &result.Mapping);
    status = MemorySpaceCopyUser(resultOut, &result, sizeof(result), true);
    if (status != OS_EOK) {
        ERROR("ScDmaAllocate: MemorySpaceCopyUser mapping %u result failed: %u", record->Handle, status);
        goto error;
    }

    TRACE("ScDmaAllocate: publishing mapping %u, context %u, SHM %u, segments %u",
          record->Handle, context, result.Buffer.ID, result.Mapping.SegmentCount);
    __Publish(record);
    __End(parent);
    return OS_EOK;

error:
    // Unpublished allocations have never been handed to hardware.
    cleanupStatus = SHMDeviceUnmap(&record->Mapping);
    if (cleanupStatus != OS_EOK) {
        ERROR("ScDmaAllocate: SHMDeviceUnmap mapping %u rollback failed: %u (original status %u)",
              record->Handle, cleanupStatus, status);
    }

    if (result.Buffer.ID != UUID_INVALID) {
        cleanupStatus = SHMDetach(&result.Buffer);
        if (cleanupStatus != OS_EOK) {
            ERROR("ScDmaAllocate: SHMDetach buffer %u rollback failed: %u (original status %u)",
                  result.Buffer.ID, cleanupStatus, status);
        }
    }
    
    __Remove(record);
    __End(parent);
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
    struct __DmaRecord*       parent;
    struct __DmaRecord*       record;
    oserr_t                   status;
    oserr_t                   cleanupStatus;

    TRACE("ScDmaMapShm(context=%u, shm=%u)", context, shm);
    status = MemorySpaceCopyUser((void*)userParameters, &parameters, sizeof(parameters), false);
    if (status != OS_EOK) {
        ERROR("ScDmaMapShm: MemorySpaceCopyUser parameters for context %u, SHM %u failed: %u",
              context, shm, status);
        return status;
    }

    if (parameters.Flags & ~DMA_MAP_REQUIRE_DIRECT) {
        return OS_EINVALPARAMS;
    }

    status = __Begin(context, 0, &parent);
    if (status != OS_EOK) {
        return status;
    }

    status = __Create(&record);
    if (status != OS_EOK) {
        ERROR("ScDmaMapShm: __Create context %u, SHM %u failed: %u", context, shm, status);
        __End(parent);
        return status;
    }

    record->Parent = parent;
    if (parameters.Flags & DMA_MAP_REQUIRE_DIRECT) {
        status = SHMDeviceMapDirect(
            parent->Context,
            shm,
            parameters.Offset,
            parameters.Length,
            parameters.Direction,
            &record->Mapping
        );
    } else {
        status = SHMDeviceMap(
            parent->Context,
            shm,
            parameters.Offset,
            parameters.Length,
            parameters.Direction,
            &record->Mapping
        );
    }
    if (status != OS_EOK) {
        ERROR("ScDmaMapShm: SHMDeviceMap context %u, SHM %u, offset %zu, length %zu, direction %u failed: %u",
              context, shm, parameters.Offset, parameters.Length, parameters.Direction, status);
        goto error;
    }

    __MappingResult(record, &result);
    status = MemorySpaceCopyUser(resultOut, &result, sizeof(result), true);
    if (status != OS_EOK) {
        ERROR("ScDmaMapShm: MemorySpaceCopyUser mapping %u result failed: %u", record->Handle, status);
        goto error;
    }

    TRACE("ScDmaMapShm: publishing mapping %u, context %u, SHM %u, segments %u",
          record->Handle, context, shm, result.SegmentCount);
    __Publish(record);
    __End(parent);
    return OS_EOK;

error:
    cleanupStatus = SHMDeviceUnmap(&record->Mapping);
    if (cleanupStatus != OS_EOK) {
        ERROR("ScDmaMapShm: SHMDeviceUnmap mapping %u rollback failed: %u (original status %u)",
              record->Handle, cleanupStatus, status);
    }
    __Remove(record);
    __End(parent);
    return status;
}

oserr_t
ScDmaGetSegments(
    _In_  uuid_t             mapping,
    _Out_ struct DMASegment* entries,
    _In_  uint32_t           capacity,
    _Out_ uint32_t*          countOut)
{
    struct __DmaRecord*      record;
    const struct DMASegment* segments;
    uint32_t                 count;
    oserr_t                  status;

    TRACE("ScDmaGetSegments(mapping=%u, capacity=%u, entries=%p)", mapping, capacity, entries);
    status = __Begin(mapping, 1, &record);
    if (status != OS_EOK) {
        return status;
    }

    segments = SHMDeviceMappingSegments(record->Mapping, &count);

    status = MemorySpaceCopyUser(countOut, &count, sizeof(count), true);
    if (status != OS_EOK) {
        ERROR("ScDmaGetSegments: MemorySpaceCopyUser count for mapping %u failed: %u", mapping, status);
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

    status = MemorySpaceCopyUser(entries, (void*)segments, count * sizeof(*segments), true);
    if (status != OS_EOK) {
        ERROR("ScDmaGetSegments: MemorySpaceCopyUser %u segments for mapping %u failed: %u", count, mapping, status);
        goto exit;
    }

exit:
    __End(record);
    return status;
}

static oserr_t
__Sync(
    _In_ uuid_t mapping,
    _In_ size_t offset,
    _In_ size_t length,
    _In_ int    forDevice)
{
    struct __DmaRecord* record;
    oserr_t             status;

    status = __Begin(mapping, 1, &record);
    if (status != OS_EOK) {
        return status;
    }

    if (forDevice) {
        status = SHMDeviceSyncForDevice(record->Mapping, offset, length);
        if (status != OS_EOK) {
            WARNING("__Sync: SHMDeviceSyncForDevice mapping %u, offset %zu, length %zu failed: %u",
                    mapping, offset, length, status);
        }
    } else {
        status = SHMDeviceSyncForCpu(record->Mapping, offset, length);
        if (status != OS_EOK) {
            WARNING("__Sync: SHMDeviceSyncForCpu mapping %u, offset %zu, length %zu failed: %u",
                    mapping, offset, length, status);
        }
    }

    __End(record);
    return status;
}

oserr_t
ScDmaSyncForDevice(
    _In_ uuid_t mapping,
    _In_ size_t offset,
    _In_ size_t length)
{
    TRACE("ScDmaSyncForDevice(mapping=%u, offset=%zu, length=%zu)", mapping, offset, length);
    return __Sync(mapping, offset, length, 1);
}

oserr_t
ScDmaSyncForCpu(
    _In_ uuid_t mapping,
    _In_ size_t offset,
    _In_ size_t length)
{
    TRACE("ScDmaSyncForCpu(mapping=%u, offset=%zu, length=%zu)", mapping, offset, length);
    return __Sync(mapping, offset, length, 0);
}

oserr_t
ScDmaUnmap(
    _In_ uuid_t mapping)
{
    struct __DmaRecord* record;
    oserr_t             status;

    TRACE("ScDmaUnmap(mapping=%u)", mapping);
    status = __Begin(mapping, 1, &record);
    if (status != OS_EOK) {
        return status;
    }

    status = SHMDeviceUnmap(&record->Mapping);
    if (status != OS_EOK) {
        WARNING("ScDmaUnmap: SHMDeviceUnmap mapping %u failed: %u; record retained", mapping, status);
        __End(record);
        return status;
    }

    __Remove(record);
    TRACE("ScDmaUnmap: mapping %u released", mapping);
    return OS_EOK;
}

oserr_t
ScDmaClose(
    _In_ uuid_t context)
{
    struct __DmaRecord* record;
    oserr_t             status;
    unsigned int        mappings;

    TRACE("ScDmaClose(context=%u)", context);
    status = __Begin(context, 0, &record);
    if (status != OS_EOK) {
        return status;
    }

    MutexLock(&g_dmaLock);
    mappings = record->Mappings;
    MutexUnlock(&g_dmaLock);
    if (mappings != 0) {
        TRACE("ScDmaClose: context %u still owns %u mappings", context, mappings);
        __End(record);
        return OS_EBUSY;
    }

    SHMDeviceContextRelease(&record->Context);
    __Remove(record);
    TRACE("ScDmaClose: context %u released", context);
    return OS_EOK;
}
