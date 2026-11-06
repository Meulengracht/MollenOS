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

/* Block request DMA buffers, queue iterations, and completion. */
#define __TRACE
#define __need_static_assert
#include "private.h"

#include <ddk/barrier.h>
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/types/storage.h>
#include <stdlib.h>
#include <string.h>
#include <ctt_storage_service_server.h>

// Each direct request chain starts with a 16-byte device-readable header and
// ends with one device-writable status byte in the same metadata allocation.
#define VIRTIO_BLK_STATUS_OFFSET   sizeof(VirtioBlkRequestHeader_t)
#define VIRTIO_BLK_METADATA_SIZE   (VIRTIO_BLK_STATUS_OFFSET + sizeof(uint8_t))
// Reserve one descriptor for the header and one for the status byte.
#define VIRTIO_BLK_CHAIN_OVERHEAD  2

COMPILE_TIME_ASSERT(sizeof(VirtioBlkRequestHeader_t) == 16);

/**
 * @brief Tracks one storage transfer across one or more queue chains.
 *
 * The previous chain must complete before Metadata can be reused. Data stays
 * attached for the entire transfer. DeferredMessage is used to send one response
 * after the transfer completes or fails and all request resources are released.
 */
typedef struct VirtioBlkRequest {
    VirtioBlkDevice_t*    Device;             // Device and queue serving the transfer.
    struct DMABuffer*     Metadata;
    uint64_t              MetadataAddress;
    OSHandle_t            Data;
    struct DMAMapping*    Mapping;
    const struct DMASegment* Segments;
    uint32_t              SegmentCount;
    struct VirtioBlkRequest* Next;
    oserr_t               Result;
    bool                  DeviceOwned;
    bool                  Deferred;
    uint64_t              Sector;             // First logical sector of the full transfer.
    size_t                SectorCount;        // Total logical sectors requested.
    size_t                SectorsTransferred; // Successfully completed logical sectors.
    size_t                IterationSectors;   // Logical sectors in the published chain.
    int                   Direction;          // __STORAGE_OPERATION_* value.
    struct gracht_message DeferredMessage[];  // Message saved until the final response.
} VirtioBlkRequest_t;

static oserr_t
__DestroyRequest(
    _In_ VirtioBlkRequest_t* request)
{
    oserr_t status;

    // The request must have completed or the device must be reset before cleanup.
    // DMA covers all queue iterations. On error, copy back only completed sectors.
    // Keep the request if cleanup fails so we can retry.
    if (request->DeviceOwned) {
        status = DMAEndTransfer(request->Mapping,
            request->SectorsTransferred * request->Device->Descriptor.SectorSize);
        if (status != OS_EOK) {
            return status;
        }
        request->DeviceOwned = false;
    }

    status = DMAUnmap(&request->Mapping);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DMAFree(&request->Metadata);
    if (status != OS_EOK) {
        return status;
    }
    
    status = SHMRelease(&request->Data);
    if (status != OS_EOK) {
        return status;
    }
    
    if (request->Deferred) {
        ctt_storage_transfer_response(&request->DeferredMessage[0],
            request->Result, request->SectorsTransferred);
    }
    free(request);
    return OS_EOK;
}

static void
__CleanupRequest(
    _In_ VirtioBlkRequest_t* request)
{
    oserr_t status;

    status = __DestroyRequest(request);
    if (status != OS_EOK) {
        // Once a used entry is removed, the queue can no longer find its request.
        // Keep failed requests on the device's cleanup list for later retries.
        WARNING("Virtio block DMA cleanup deferred: %u", status);
        request->Next = request->Device->CleanupRequests;
        request->Device->CleanupRequests = request;
    }
}

oserr_t
VirtioBlkRetryRequests(
    _InOut_ VirtioBlkDevice_t* device)
{
    VirtioBlkRequest_t* request = device->CleanupRequests;
    VirtioBlkRequest_t* next;
    oserr_t             status;
    oserr_t             result = OS_EOK;

    // Take the current list before cleanup can yield. Try each request once
    // and put it back on the list if cleanup fails.
    device->CleanupRequests = NULL;
    while (request != NULL) {
        next = request->Next;
        
        status = __DestroyRequest(request);
        if (status != OS_EOK) {
            request->Next = device->CleanupRequests;
            device->CleanupRequests = request;
            result = status;
        }
        
        request = next;
    }
    return result;
}

/**
 * Allocate and attach DMA resources before deferring the RPC. On failure,
 * the caller must respond to the original message. On success, request cleanup
 * sends the response, including when queue submission fails.
 */
static oserr_t
__CreateRequest(
    _In_  VirtioBlkDevice_t*      device,
    _In_  struct gracht_message*  message,
    _In_  int                     direction,
    _In_  uint64_t                sector,
    _In_  uuid_t                  bufferId,
    _In_  size_t                  bufferOffset,
    _In_  size_t                  byteCount,
    _Out_ VirtioBlkRequest_t**    requestOut)
{
    VirtioBlkRequest_t* request;
    size_t              requestSize;
    size_t              deferredMessageSize = GRACHT_MESSAGE_DEFERRABLE_SIZE(message);
    oserr_t             oserr;
    TRACE("__CreateRequest(sector=%" PRIu64 ", bytes=%" PRIuIN ")", sector, byteCount);

    *requestOut = NULL;
    if (deferredMessageSize > SIZE_MAX - sizeof(VirtioBlkRequest_t)) {
        return OS_EOVERFLOW;
    }

    requestSize = sizeof(VirtioBlkRequest_t) + deferredMessageSize;
    request = calloc(1, requestSize);
    if (request == NULL) {
        return OS_EOOM;
    }
    request->Data.ID = UUID_INVALID;
    request->Device = device;
    request->Direction = direction;
    request->Sector = sector;
    request->SectorCount = byteCount / device->Descriptor.SectorSize;

    // The header and status byte stay shared with the device across iterations.
    // Access requires barriers, but no DMA begin/end calls for this allocation.
    oserr = DMAAllocate(
        device->Transport.DmaContext,
        &(struct DMARequirements) {
            .Length = VIRTIO_BLK_METADATA_SIZE, .Alignment = 16, .Contiguous = true
        },
        &request->Metadata
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    request->MetadataAddress = DMABufferSegments(request->Metadata, &request->SegmentCount)[0].Address;

    // Map the caller's shared memory for this device. The DMA layer handles
    // cache updates and staging, so the buffer needs no special physical layout.
    oserr = SHMAttach(bufferId, &request->Data);
    if (oserr != OS_EOK) {
        oserr = OS_EINVALPARAMS;
        goto error;
    }

    if (bufferOffset > SHMBufferCapacity(&request->Data) ||
        byteCount > SHMBufferCapacity(&request->Data) - bufferOffset) {
        oserr = OS_EINVALPARAMS;
        goto error;
    }

    oserr = DMAMapSHM(
        device->Transport.DmaContext,
        &request->Data,
        bufferOffset,
        byteCount,
        direction == __STORAGE_OPERATION_READ ? DMAFromDevice : DMAToDevice,
        NULL,
        &request->Mapping
    );
    if (oserr != OS_EOK) {
        goto error;
    }

    request->Segments = DMAMappingSegments(request->Mapping, &request->SegmentCount);

    // Save the message so we can send one response after all queue iterations
    // and request cleanup have finished.
    gracht_server_defer_message(message, &request->DeferredMessage[0]);
    request->Deferred = true;
    
    *requestOut = request;
    return OS_EOK;

error:
    __CleanupRequest(request);
    return oserr;
}

/**
 * Requires an unsubmitted request or a completed previous chain. Build the next
 * part of the transfer using whole sectors within the queue and device SG limits.
 * This prepares metadata for submission. The caller must free the descriptor
 * array after submitting it.
 */
static oserr_t
__BuildIteration(
    _InOut_ VirtioBlkRequest_t* request,
    _Out_   VirtioQueueBuffer_t** buffersOut,
    _Out_   uint16_t*             bufferCountOut)
{
    VirtioBlkDevice_t*        device = request->Device;
    VirtioQueueStats_t        queueStats;
    VirtioQueueBuffer_t*      buffers;
    VirtioBlkRequestHeader_t* header;
    size_t                    maximumDataDescriptors;
    size_t                    remainingBytes;
    size_t                    candidateBytes = 0;
    size_t                    iterationBytes;
    size_t                    dataOffset;
    size_t                    sgOffset;
    size_t                    dataDescriptorCount = 0;
    uint32_t                  sgIndex;
    oserr_t                   oserr;
    
    TRACE("__BuildIteration(transferred=%" PRIuIN "/%" PRIuIN ")",
        request->SectorsTransferred, request->SectorCount);

    *buffersOut = NULL;
    oserr = VirtioSplitQueueGetStats(device->RequestQueue, &queueStats);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    // We need space for the header, status byte, and at least one data segment.
    if (queueStats.FreeDescriptors <= VIRTIO_BLK_CHAIN_OVERHEAD ||
        device->SegmentMax == 0) {
        return OS_EBUSY;
    }

    // Limit data descriptors to the free queue entries and the device's seg_max.
    maximumDataDescriptors =
            queueStats.FreeDescriptors - VIRTIO_BLK_CHAIN_OVERHEAD;
    if (maximumDataDescriptors > device->SegmentMax) {
        maximumDataDescriptors = device->SegmentMax;
    }

    // Include space for the header and status descriptors.
    buffers = calloc(
        maximumDataDescriptors + VIRTIO_BLK_CHAIN_OVERHEAD,
        sizeof(VirtioQueueBuffer_t)
    );
    if (buffers == NULL) {
        return OS_EOOM;
    }

    dataOffset = request->SectorsTransferred * device->Descriptor.SectorSize;
    remainingBytes = (request->SectorCount - request->SectorsTransferred) *
            device->Descriptor.SectorSize;
    // The used-ring length is 32 bits. Limit this iteration so the completion
    // length cannot overflow when the caller requests a larger transfer.
    if (remainingBytes > UINT32_MAX - VIRTIO_BLK_METADATA_SIZE) {
        remainingBytes = UINT32_MAX - VIRTIO_BLK_METADATA_SIZE;
    }

    // Find the next byte from the number of sectors already transferred.
    // The previous chain may have ended in the middle of an SG entry.
    sgIndex = 0;
    sgOffset = dataOffset;
    while (sgIndex < request->SegmentCount && sgOffset >= request->Segments[sgIndex].Length) {
        sgOffset -= request->Segments[sgIndex++].Length;
    }

    // Fill data descriptors up to the iteration's descriptor and byte limits.
    for (uint32_t index = sgIndex;
         remainingBytes > 0 &&
             dataDescriptorCount < maximumDataDescriptors &&
             index < request->SegmentCount;
         index++) {
        size_t    available = request->Segments[index].Length - sgOffset;
        uint64_t  address = request->Segments[index].Address + sgOffset;

        while (available > 0 && remainingBytes > 0 &&
               dataDescriptorCount < maximumDataDescriptors) {
            size_t length = available < remainingBytes ? available : remainingBytes;
            if (length > device->SizeMax) {
                length = device->SizeMax;
            }

            buffers[dataDescriptorCount + 1].Address = address;
            buffers[dataDescriptorCount + 1].Length = (uint32_t)length;
            buffers[dataDescriptorCount + 1].Flags =
                    request->Direction == __STORAGE_OPERATION_READ ?
                            VIRTIO_SPLIT_DESC_F_WRITE : 0;
            dataDescriptorCount++;
            candidateBytes += length;
            remainingBytes -= length;
            available -= length;
            address += length;
        }
        sgOffset = 0;
    }

    // Round down to whole device sectors.
    iterationBytes = candidateBytes -
            (candidateBytes % device->Descriptor.SectorSize);
    if (iterationBytes == 0) {
        free(buffers);
        return OS_ENOTSUPPORTED;
    }

    // size_max or an SG boundary may leave a partial sector at the end.
    // Remove those bytes from the descriptors before submitting the chain.
    for (size_t excess = candidateBytes - iterationBytes; excess > 0;) {
        VirtioQueueBuffer_t* buffer = &buffers[dataDescriptorCount];
        if (buffer->Length <= excess) {
            excess -= buffer->Length;
            memset(buffer, 0, sizeof(*buffer));
            dataDescriptorCount--;
        }
        else {
            buffer->Length -= (uint32_t)excess;
            excess = 0;
        }
    }

    request->IterationSectors = iterationBytes / device->Descriptor.SectorSize;
    header = DMABufferData(request->Metadata);
    header->Type = request->Direction == __STORAGE_OPERATION_READ ?
            VIRTIO_BLK_T_IN : VIRTIO_BLK_T_OUT;
    header->Reserved = 0;
    
    // The full request already fits within capacity and uint64_t.
    // Advancing by completed sectors stays within those bounds.
    header->Sector = (request->Sector + request->SectorsTransferred) *
            (device->Descriptor.SectorSize / VIRTIO_BLK_SECTOR_SIZE);
    *((uint8_t*)header + VIRTIO_BLK_STATUS_OFFSET) = UINT8_MAX;

    buffers[0].Address = request->MetadataAddress;
    buffers[0].Length = sizeof(VirtioBlkRequestHeader_t);
    buffers[dataDescriptorCount + 1].Address =
            request->MetadataAddress + VIRTIO_BLK_STATUS_OFFSET;
    buffers[dataDescriptorCount + 1].Length = sizeof(uint8_t);
    buffers[dataDescriptorCount + 1].Flags = VIRTIO_SPLIT_DESC_F_WRITE;

    TRACE("__BuildIteration type=%u, sector=%" PRIu64
        ", dataDescriptors=%" PRIuIN ", metadata=0x%" PRIx64,
        header->Type,
        header->Sector,
        dataDescriptorCount,
        request->MetadataAddress
    );
    *buffersOut = buffers;
    *bufferCountOut =
            (uint16_t)(dataDescriptorCount + VIRTIO_BLK_CHAIN_OVERHEAD);
    return OS_EOK;
}

static oserr_t
__SubmitIteration(
    _InOut_ VirtioBlkRequest_t* request)
{
    VirtioQueueBuffer_t* buffers;
    uint16_t             bufferCount;
    oserr_t              oserr;

    oserr = __BuildIteration(request, &buffers, &bufferCount);
    if (oserr != OS_EOK) {
        return oserr;
    }

    if (!request->DeviceOwned) {
        // Begin DMA once for the whole request. The client must leave the
        // mapped bytes untouched until it receives the final response.
        oserr = DMABeginTransfer(
            request->Mapping,
            0,
            request->SectorCount * request->Device->Descriptor.SectorSize,
            SHMWaitNonBlocking,
            NULL
        );
        if (oserr != OS_EOK) {
            free(buffers);
            return oserr;
        }
        request->DeviceOwned = true;
    }

    // VirtioSplitQueueSubmit publishes all preceding metadata and descriptor
    // writes with a DMA barrier before it advances the available index.
    oserr = VirtioSplitQueueSubmit(
        request->Device->RequestQueue,
        buffers,
        bufferCount,
        request,
        NULL
    );
    free(buffers);

    // If notification failed after submission, return OS_EINPROGRESS unchanged.
    // The queue blocks further submissions. The caller must reset it before
    // completing this request or releasing its DMA resources.
    return oserr;
}

static void
__FinishRequest(
    _In_ VirtioBlkRequest_t* request,
    _In_ oserr_t             oserr)
{
    uint8_t status;

    // The status byte is written asynchronously by the device. Read it through
    // the volatile helper after queue completion has established DMA ordering.
    ReadVolatileMemory(
        (const uint8_t*)DMABufferData(request->Metadata) + VIRTIO_BLK_STATUS_OFFSET,
        &status,
        sizeof(status)
    );

    // Convert the device status to oserr_t if no earlier operation failed.
    if (oserr == OS_EOK) {
        if (status == VIRTIO_BLK_S_IOERR) {
            oserr = OS_EDEVFAULT;
        } else if (status == VIRTIO_BLK_S_UNSUPP) {
            oserr = OS_ENOTSUPPORTED;
        } else if (status != VIRTIO_BLK_S_OK) {
            oserr = OS_EPROTOCOL;
        }
    }

    request->Result = oserr;
    __CleanupRequest(request);
}

oserr_t
VirtioBlkResetRequestQueue(
    _InOut_ VirtioBlkDevice_t* device)
{
    VirtioQueueCompletion_t completion;
    oserr_t                 oserr;
    TRACE("VirtioBlkResetRequestQueue()");

    // Reset must finish before we cancel outstanding chains. The device may
    // still access queue memory until then.
    oserr = VirtioPciReset(&device->Transport);
    if (oserr != OS_EOK) {
        ERROR("Failed to reset Virtio block request queue: %u", oserr);
        return oserr;
    }
    
    while (device->RequestQueue != NULL &&
           VirtioSplitQueueAbort(device->RequestQueue, &completion) == OS_EOK) {
        __FinishRequest(completion.Context, OS_ECANCELLED);
    }

    oserr = VirtioBlkRetryRequests(device);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    if (device->RequestQueue == NULL) {
        return OS_EOK;
    }

    // All aborted requests must be cleaned up and their responses sent before
    // we destroy the queue.
    oserr = VirtioSplitQueueDestroy(device->RequestQueue);
    if (oserr == OS_EOK) {
        device->RequestQueue = NULL;
    }
    return oserr;
}

oserr_t
VirtioBlkDeviceTransfer(
    _In_ VirtioBlkDevice_t*      device,
    _In_ struct gracht_message*  message,
    _In_ int                     direction,
    _In_ uint64_t                sector,
    _In_ uuid_t                  bufferId,
    _In_ size_t                  bufferOffset,
    _In_ size_t                  sectorCount)
{
    VirtioBlkRequest_t* request;
    size_t              byteCount;
    oserr_t             oserr;
    TRACE("VirtioBlkDeviceTransfer(sector=%" PRIu64 ", sectors=%" PRIuIN ")",
        sector, sectorCount);

    if (device == NULL || message == NULL || sectorCount == 0 ||
        (direction != __STORAGE_OPERATION_READ &&
         direction != __STORAGE_OPERATION_WRITE) ||
        sector >= device->Descriptor.SectorCount ||
        sectorCount > device->Descriptor.SectorCount - sector ||
        sectorCount > SIZE_MAX / device->Descriptor.SectorSize) {
        return OS_EINVALPARAMS;
    }
    
    if (direction == __STORAGE_OPERATION_WRITE &&
        (device->Descriptor.Flags & STORAGE_READONLY)) {
        return OS_ENOTSUPPORTED;
    }
    
    if (sector > UINT64_MAX /
            (device->Descriptor.SectorSize / VIRTIO_BLK_SECTOR_SIZE)) {
        return OS_EOVERFLOW;
    }

    oserr = VirtioBlkRetryRequests(device);
    if (oserr != OS_EOK) {
        return oserr;
    }
    byteCount = sectorCount * device->Descriptor.SectorSize;
    oserr = __CreateRequest(
        device,
        message,
        direction,
        sector,
        bufferId,
        bufferOffset,
        byteCount,
        &request
        );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = __SubmitIteration(request);
    if (oserr == OS_EINPROGRESS) {
        // The request is already in the queue. Recovery aborts it and
        // sends the deferred cancellation response before rebuilding the queue.
        (void)VirtioBlkRecoverDevice(device);
        return OS_EOK;
    }
    if (oserr != OS_EOK) {
        __FinishRequest(request, oserr);
        return OS_EOK;
    }
    return OS_EOK;
}

oserr_t
VirtioBlkDeviceHandleInterrupt(
    _In_ VirtioBlkDevice_t* device)
{
    VirtioQueueCompletion_t completion;
    oserr_t                 oserr;

    if (device == NULL) {
        return OS_EINVALPARAMS;
    }
    
    oserr = VirtioBlkRetryRequests(device);
    if (oserr != OS_EOK) {
        return oserr;
    }

    for (;;) {
        VirtioBlkRequest_t* request;
        uint8_t             requestStatus;
        size_t              iterationBytes;
        uint32_t            expectedWritten;

        oserr = VirtioSplitQueuePoll(device->RequestQueue, &completion);
        if (oserr == OS_ENOENT) {
            return OS_EOK;
        }
        if (oserr != OS_EOK) {
            return oserr;
        }

        // VirtioSplitQueuePoll orders all device writes before returning the
        // used entry. Read the device-written request status only afterwards.
        request = completion.Context;
        iterationBytes = request->IterationSectors * device->Descriptor.SectorSize;

        // Reads write data and status. Writes only update the status byte.
        if (request->Direction == __STORAGE_OPERATION_READ) {
            expectedWritten = (uint32_t)(iterationBytes + sizeof(uint8_t));
        } else {
            expectedWritten = sizeof(uint8_t);
        }

        ReadVolatileMemory(
            (const uint8_t*)DMABufferData(request->Metadata) + VIRTIO_BLK_STATUS_OFFSET,
            &requestStatus,
            sizeof(requestStatus)
        );

        if (requestStatus != VIRTIO_BLK_S_OK) {
            // Convert the device status to a storage error. Request cleanup
            // sends the deferred response once all resources are released.
            __FinishRequest(request, OS_EOK);
            continue;
        }
        if (completion.Length != expectedWritten) {
            __FinishRequest(request, OS_EPROTOCOL);
            continue;
        }

        request->SectorsTransferred += request->IterationSectors;
        if (request->SectorsTransferred == request->SectorCount) {
            __FinishRequest(request, OS_EOK);
            continue;
        }

        oserr = __SubmitIteration(request);
        if (oserr == OS_EINPROGRESS) {
            // The chain was submitted, but notifying the device failed.
            // Recovery must reset the queue before completing this request.
            return VirtioBlkRecoverDevice(device);
        }
        
        if (oserr != OS_EOK) {
            __FinishRequest(request, oserr);
        }
    }
}
