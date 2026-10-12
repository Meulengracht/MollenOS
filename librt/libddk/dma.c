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

#include <ddk/dma.h>
#include <internal/_syscalls.h>
#include <os/handle.h>
#include <os/types/syscall.h>
#include <stdlib.h>

struct DMAMapping {
    uuid_t                   Handle;
    size_t                   Length;
    bool                     Allocation;
    uint32_t                 SegmentCount;
    struct SHMDeviceSegment* Segments;
};

oserr_t
DMAOpen(
    _In_  uuid_t   deviceGrant,
    _In_  uint64_t addressLimit,
    _Out_ uuid_t*  handleOut)
{
    uuid_t  handle = UUID_INVALID;
    oserr_t status;

    // An occupied slot is already an owner; replacing it would lose that owner.
    if (handleOut == NULL || deviceGrant == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }
    if (*handleOut != UUID_INVALID) {
        return OS_EBUSY;
    }
    
    // Pass the limit by address so its upper bits survive a 32-bit syscall ABI.
    status = Syscall_DmaOpen(deviceGrant, &addressLimit, &handle);
    if (status != OS_EOK) {
        return status;
    }
    *handleOut = handle;
    return OS_EOK;
}

oserr_t
DMAAllocate(
    _In_    uuid_t                              dmaHandle,
    _In_    const struct SHMDeviceRequirements* requirements,
    _InOut_ OSHandle_t*                         bufferOut,
    _InOut_ struct DMAMapping**                 mappingOut)
{
    struct OSDmaAllocationResult result = { 0 };
    struct DMAMapping*           mapping;
    SHMHandle_t*                 buffer;
    oserr_t                      status;

    // Keep the SHM attachment and the device mapping as distinct owners. Neither
    // output may replace an existing object, including an unmapped SHM handle.
    if (dmaHandle == UUID_INVALID || requirements == NULL || bufferOut == NULL || mappingOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*mappingOut != NULL || bufferOut->ID != UUID_INVALID ||
        bufferOut->Type != OSHANDLE_NULL || bufferOut->Payload != NULL) {
        return OS_EBUSY;
    }

    // Reserve all local bookkeeping before the kernel creates resources. Segment
    // storage is fetched separately, so a later query failure cannot lose these.
    mapping = calloc(1, sizeof(*mapping));
    buffer = malloc(sizeof(*buffer));
    if (mapping == NULL || buffer == NULL) {
        free(mapping);
        free(buffer);
        return OS_EOOM;
    }
    
    status = Syscall_DmaAllocate(dmaHandle, requirements, &result);
    if (status != OS_EOK) {
        free(mapping);
        free(buffer);
        return status;
    }
    *buffer = result.Buffer;
    mapping->Handle = result.Mapping.Handle;
    mapping->Length = result.Buffer.Length;
    mapping->Allocation = true;
    mapping->SegmentCount = result.Mapping.SegmentCount;

    // Reuse libos's SHM destruction/mapping behavior. OSHandleWrap registers an
    // existing payload and currently returns OS_EOK unconditionally; it does not
    // create a second kernel attachment or take a second SHM reference.
    (void)OSHandleWrap(buffer->ID, OSHANDLE_SHM, buffer, true, bufferOut);
    *mappingOut = mapping;
    return OS_EOK;
}

oserr_t
DMAMapSHM(
    _In_    uuid_t                    dmaHandle,
    _In_    const OSHandle_t*        buffer,
    _In_    size_t                   offset,
    _In_    size_t                   length,
    _In_    enum SHMDeviceDirection direction,
    _InOut_ struct DMAMapping**      mappingOut)
{
    struct OSDmaMapParameters parameters = {
        .Offset = offset,
        .Length = length,
        .Direction = direction
    };
    struct OSDmaMappingResult result = { 0 };
    struct DMAMapping*        mapping;
    oserr_t                   status;

    // Only SHM identities are meaningful here. This local check catches driver
    // mistakes; the kernel must independently check the caller's buffer rights.
    if (dmaHandle == UUID_INVALID || buffer == NULL || mappingOut == NULL || length == 0) {
        return OS_EINVALPARAMS;
    }
    if (buffer->Type != OSHANDLE_SHM || buffer->ID == UUID_INVALID ||
        direction < SHMDeviceToDevice || direction > SHMDeviceBidirectional) {
        return OS_EINVALPARAMS;
    }
    if (*mappingOut != NULL) {
        return OS_EBUSY;
    }
   
    mapping = calloc(1, sizeof(struct DMAMapping));
    if (mapping == NULL) {
        return OS_EOOM;
    }
    
    status = Syscall_DmaMapShm(dmaHandle, buffer->ID, &parameters, &result);
    if (status != OS_EOK) {
        free(mapping);
        return status;
    }
    
    mapping->Handle = result.Handle;
    mapping->Length = length;
    mapping->SegmentCount = result.SegmentCount;
    *mappingOut = mapping;
    return OS_EOK;
}

oserr_t
DMAGetSegments(
    _In_  struct DMAMapping*              mapping,
    _Out_ const struct SHMDeviceSegment** segmentsOut,
    _Out_ uint32_t*                       countOut)
{
    uint32_t                 count = 0;
    struct SHMDeviceSegment* segments;
    size_t                   bytes;
    oserr_t                  status;

    // The kernel list is immutable for the mapping's lifetime. Keep a local
    // copy so we can avoid calling the kernel again.
    if (mapping == NULL || segmentsOut == NULL || countOut == NULL) {
        return OS_EINVALPARAMS;
    }

    if (mapping->Segments == NULL) {
        bytes = (size_t)mapping->SegmentCount * sizeof(*segments);
        if (mapping->SegmentCount == 0 || bytes / sizeof(*segments) != mapping->SegmentCount) {
            return OS_EOVERFLOW;
        }
        
        segments = malloc(bytes);
        if (segments == NULL) {
            return OS_EOOM;
        }
        
        status = Syscall_DmaGetSegments(mapping->Handle, segments, mapping->SegmentCount, &count);
        if (status == OS_EOK && count != mapping->SegmentCount) {
            status = OS_EUNKNOWN;
        }
        if (status != OS_EOK) {
            free(segments);
            return status;
        }
        mapping->Segments = segments;
    }

    *segmentsOut = mapping->Segments;
    *countOut = mapping->SegmentCount;
    return OS_EOK;
}

static bool
__SyncRangeValid(
    _In_ const struct DMAMapping* mapping,
    _In_ size_t                   offset,
    _In_ size_t                   length)
{
    // Continuous allocations cannot follow a whole-buffer transfer handoff.
    // Share only range validation; each handoff has its own syscall and kernel
    // ownership checks, including when a caller bypasses libddk.
    return mapping != NULL && !mapping->Allocation &&
        offset <= mapping->Length && length <= mapping->Length - offset;
}

oserr_t
DMASyncForDevice(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             offset,
    _In_ size_t             length)
{
    // The kernel performs cache work and any bounce copy before device ownership.
    if (length == 0 || !__SyncRangeValid(mapping, offset, length)) {
        return OS_EINVALPARAMS;
    }
    return Syscall_DmaSyncForDevice(mapping->Handle, offset, length);
}

oserr_t
DMASyncForCpu(
    _In_ struct DMAMapping* mapping,
    _In_ size_t             offset,
    _In_ size_t             length)
{
    // Pass the completed length unchanged so a short receive never copies back
    // the uncompleted tail, and a cancelled transfer can reclaim zero bytes.
    if (!__SyncRangeValid(mapping, offset, length)) {
        return OS_EINVALPARAMS;
    }
    return Syscall_DmaSyncForCpu(mapping->Handle, offset, length);
}

oserr_t
DMAUnmap(
    _InOut_ struct DMAMapping** mapping)
{
    struct DMAMapping* dma;
    oserr_t            status;

    // Never discard local ownership if the kernel reports busy or another error.
    // That preserves the only handle with which the caller can retry cleanup.
    if (mapping == NULL || *mapping == NULL) {
        return OS_EOK;
    }
    
    dma = *mapping;
    
    status = Syscall_DmaUnmap(dma->Handle);
    if (status != OS_EOK) {
        return status;
    }

    *mapping = NULL;
    free(dma->Segments);
    free(dma);
    return OS_EOK;
}

oserr_t
DMAClose(
    _In_ uuid_t dmaHandle)
{
    if (dmaHandle == UUID_INVALID) {
        return OS_EOK;
    }
    return Syscall_DmaClose(dmaHandle);
}
