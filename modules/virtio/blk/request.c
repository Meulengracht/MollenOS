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

/* DMA request ownership, SG iteration construction, and completion. */
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
// The header and status each consume one descriptor in addition to data SGs.
#define VIRTIO_BLK_CHAIN_OVERHEAD  2

COMPILE_TIME_ASSERT(sizeof(VirtioBlkRequestHeader_t) == 16);

/**
 * @brief Owns one logical storage transfer across one or more queue chains.
 *
 * Metadata is reused only after the device returns the previous chain. Data
 * remains attached for the entire transfer, and DeferredMessage receives one
 * final response after all sectors complete or an iteration fails.
 */
typedef struct VirtioBlkRequest {
    VirtioBlkDevice_t*    Device;             // Device and queue serving the transfer.
    OSHandle_t            Metadata;           // DMA request header and status allocation.
    SHMSGTable_t          MetadataSg;         // Physical address of Metadata.
    OSHandle_t            Data;               // Attachment to the caller's data buffer.
    SHMSGTable_t          DataSg;             // Physical segments backing Data.
    uint64_t              Sector;             // First logical sector of the full transfer.
    size_t                BufferOffset;       // First byte in Data used by the transfer.
    size_t                SectorCount;        // Total logical sectors requested.
    size_t                SectorsTransferred; // Successfully completed logical sectors.
    size_t                IterationSectors;   // Logical sectors in the published chain.
    int                   Direction;          // __STORAGE_OPERATION_* value.
    struct gracht_message DeferredMessage[];  // Response retained until final completion.
} VirtioBlkRequest_t;

static void
__DestroyRequest(
    _In_ VirtioBlkRequest_t* request)
{
    if (request == NULL) {
        return;
    }
    OSHandleDestroy(&request->Data);
    OSHandleDestroy(&request->Metadata);
    free(request->DataSg.Entries);
    free(request->MetadataSg.Entries);
    free(request);
}

/**
 * Allocate and attach DMA resources before deferring the RPC. On failure the
 * original message still belongs to the invocation; on success this request
 * owns the final response even if submission subsequently fails.
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
    size_t requestSize;
    size_t deferredMessageSize = GRACHT_MESSAGE_DEFERRABLE_SIZE(message);
    oserr_t oserr;
        TRACE("__CreateRequest(sector=%" PRIu64 ", bytes=%" PRIuIN ")",
            sector, byteCount);

    *requestOut = NULL;
    if (deferredMessageSize > SIZE_MAX - sizeof(VirtioBlkRequest_t)) {
        return OS_EOVERFLOW;
    }
    requestSize = sizeof(VirtioBlkRequest_t) + deferredMessageSize;
    request = calloc(1, requestSize);
    if (request == NULL) {
        return OS_EOOM;
    }
    request->Metadata.ID = UUID_INVALID;
    request->Data.ID = UUID_INVALID;
    request->Device = device;
    request->Direction = direction;
    request->Sector = sector;
    request->BufferOffset = bufferOffset;
    request->SectorCount = byteCount / device->Descriptor.SectorSize;

    // The request header and device-written status byte are reused for each
    // iteration. They must remain device-visible until the final completion.
    // One contiguous SG entry lets the chain refer to both with base+offset
    // addresses and avoids allocating per-iteration metadata.
    oserr = SHMCreate(
        &(SHM_t) {
            .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
            .Conformity = OSMEMORYCONFORMITY_BITS32,
            .Size = VIRTIO_BLK_METADATA_SIZE,
            .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
        },
        &request->Metadata
    );
    if (oserr != OS_EOK) {
        ERROR("Failed to allocate Virtio block request metadata: %u", oserr);
        goto error;
    }
    oserr = SHMGetSGTable(&request->Metadata, &request->MetadataSg, -1);
    if (oserr != OS_EOK || request->MetadataSg.Count != 1 ||
        request->MetadataSg.Entries[0].Length < VIRTIO_BLK_METADATA_SIZE) {
        oserr = oserr == OS_EOK ? OS_ENOTSUPPORTED : oserr;
        ERROR("Virtio block metadata does not have contiguous DMA geometry");
        goto error;
    }

    // Attach rather than copy the caller's buffer. Its SG table is intentionally
    // allowed to be fragmented; __BuildIteration translates each physical
    // segment into one or more direct Virtio descriptors.
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
    oserr = SHMGetSGTable(&request->Data, &request->DataSg, -1);
    if (oserr != OS_EOK) {
        goto error;
    }
    if (request->DataSg.Count <= 0 || request->DataSg.Entries == NULL) {
        oserr = OS_EINVALPARAMS;
        goto error;
    }

    // The storage RPC receives exactly one response. Deferring it here keeps
    // that response alive across every queue iteration of a large transfer.
    gracht_server_defer_message(message, &request->DeferredMessage[0]);
    *requestOut = request;
    return OS_EOK;

error:
    __DestroyRequest(request);
    return oserr;
}

/**
 * Build one sector-aligned prefix of the remaining transfer within the queue
 * and device SG limits. This prepares metadata but does not publish it: the
 * caller owns the returned descriptor array and frees it after submission.
 * Reuse is safe only before the first submission or after its chain completes.
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
    int                       sgIndex;
    oserr_t                   oserr;
    
    TRACE("__BuildIteration(transferred=%" PRIuIN "/%" PRIuIN ")",
        request->SectorsTransferred, request->SectorCount);

    *buffersOut = NULL;
    oserr = VirtioSplitQueueGetStats(device->RequestQueue, &queueStats);
    if (oserr != OS_EOK) {
        return oserr;
    }
    
    // If there are not enough free descriptors to cover the chain overhead or
    // the device does not support any segments, we cannot build an iteration.
    if (queueStats.FreeDescriptors <= VIRTIO_BLK_CHAIN_OVERHEAD ||
        device->SegmentMax == 0) {
        return OS_EBUSY;
    }

    // Compute the maximum number of data descriptors we can use for this iteration.
    maximumDataDescriptors =
            queueStats.FreeDescriptors - VIRTIO_BLK_CHAIN_OVERHEAD;
    if (maximumDataDescriptors > device->SegmentMax) {
        maximumDataDescriptors = device->SegmentMax;
    }

    // Every chain spends one descriptor on the request header and one on the
    // status byte. The remainder is bounded by both available descriptors and
    // the device's negotiated seg_max limit.
    buffers = calloc(
        maximumDataDescriptors + VIRTIO_BLK_CHAIN_OVERHEAD,
        sizeof(VirtioQueueBuffer_t)
    );
    if (buffers == NULL) {
        return OS_EOOM;
    }

    dataOffset = request->BufferOffset +
            (request->SectorsTransferred * device->Descriptor.SectorSize);
    remainingBytes = (request->SectorCount - request->SectorsTransferred) *
            device->Descriptor.SectorSize;
    // The used-ring length is 32 bits; bound the complete chain so completion
    // accounting cannot wrap even when the caller supplied a larger transfer.
    if (remainingBytes > UINT32_MAX - VIRTIO_BLK_METADATA_SIZE) {
        remainingBytes = UINT32_MAX - VIRTIO_BLK_METADATA_SIZE;
    }

    // Recompute the SG position from cumulative progress for every iteration.
    // This is necessary when a previous chain ended in the middle of an SG
    // entry rather than exactly at a physical-segment boundary.
    oserr = SHMSGTableOffset(&request->DataSg, dataOffset, &sgIndex, &sgOffset);
    if (oserr != OS_EOK) {
        free(buffers);
        return oserr;
    }

    // Build the scatter-gather list for the current iteration, respecting the
    // maximum number of data descriptors and the remaining bytes to transfer.
    for (int index = sgIndex;
         remainingBytes > 0 &&
             dataDescriptorCount < maximumDataDescriptors &&
             index < request->DataSg.Count;
         index++) {
        size_t    available = request->DataSg.Entries[index].Length - sgOffset;
        uintptr_t address = request->DataSg.Entries[index].Address + sgOffset;

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

    // Determine the number of bytes that will actually be transferred in this iteration,
    // rounding down to the nearest multiple of the device's sector size.
    iterationBytes = candidateBytes -
            (candidateBytes % device->Descriptor.SectorSize);
    if (iterationBytes == 0) {
        free(buffers);
        return OS_ENOTSUPPORTED;
    }

    // The storage contract transfers complete logical sectors. Remove any
    // partial-sector tail introduced by size_max or an SG boundary from the
    // final descriptors before publishing the chain.
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
    header = SHMBuffer(&request->Metadata);
    header->Type = request->Direction == __STORAGE_OPERATION_READ ?
            VIRTIO_BLK_T_IN : VIRTIO_BLK_T_OUT;
    header->Reserved = 0;
    // The initial request range was checked against capacity and uint64_t
    // conversion limits, so advancing within it remains safe here.
    header->Sector = (request->Sector + request->SectorsTransferred) *
            (device->Descriptor.SectorSize / VIRTIO_BLK_SECTOR_SIZE);
    *((uint8_t*)header + VIRTIO_BLK_STATUS_OFFSET) = UINT8_MAX;

    buffers[0].Address = request->MetadataSg.Entries[0].Address;
    buffers[0].Length = sizeof(VirtioBlkRequestHeader_t);
    buffers[dataDescriptorCount + 1].Address =
            request->MetadataSg.Entries[0].Address + VIRTIO_BLK_STATUS_OFFSET;
    buffers[dataDescriptorCount + 1].Length = sizeof(uint8_t);
    buffers[dataDescriptorCount + 1].Flags = VIRTIO_SPLIT_DESC_F_WRITE;

    TRACE("__BuildIteration type=%u, sector=%" PRIu64
        ", dataDescriptors=%" PRIuIN ", metadata=0x%" PRIxIN,
        header->Type,
        header->Sector,
        dataDescriptorCount,
        request->MetadataSg.Entries[0].Address
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

    // OS_EINPROGRESS means the chain was published but its notification write
    // failed. The queue is now blocked from accepting more work, so the caller
    // must reset it before this request can be completed or its DMA attachments
    // released. Keep the result visible to the caller instead of treating the
    // request as successfully queued.
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
        (const uint8_t*)SHMBuffer(&request->Metadata) + VIRTIO_BLK_STATUS_OFFSET,
        &status,
        sizeof(status)
    );

    // Translate the device status byte into an appropriate oserr_t
    if (oserr == OS_EOK) {
        if (status == VIRTIO_BLK_S_IOERR) {
            oserr = OS_EDEVFAULT;
        } else if (status == VIRTIO_BLK_S_UNSUPP) {
            oserr = OS_ENOTSUPPORTED;
        } else if (status != VIRTIO_BLK_S_OK) {
            oserr = OS_EPROTOCOL;
        }
    }

    ctt_storage_transfer_response(
        &request->DeferredMessage[0],
        oserr,
        request->SectorsTransferred
    );
    __DestroyRequest(request);
}

oserr_t
VirtioBlkResetRequestQueue(
    _InOut_ VirtioBlkDevice_t* device)
{
    VirtioQueueCompletion_t completion;
    oserr_t oserr;
    TRACE("VirtioBlkResetRequestQueue()");

    // A device reset revokes hardware ownership of every queue allocation.
    // Only after it completes may outstanding chains be reclaimed as cancelled.
    oserr = VirtioPciReset(&device->Transport);
    if (oserr != OS_EOK) {
        ERROR("Failed to reset Virtio block request queue: %u", oserr);
        return oserr;
    }
    while (device->RequestQueue != NULL &&
           VirtioSplitQueueAbort(device->RequestQueue, &completion) == OS_EOK) {
        __FinishRequest(completion.Context, OS_ECANCELLED);
    }
    if (device->RequestQueue == NULL) {
        return OS_EOK;
    }

    // Abort returns each deferred caller context individually. Destroying the
    // queue is intentionally delayed until every final response has been sent.
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
        // The request is already owned by the queue. Recovery aborts it and
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

        // Determine the expected number of bytes written by the device for this iteration.
        if (request->Direction == __STORAGE_OPERATION_READ) {
            expectedWritten = (uint32_t)(iterationBytes + sizeof(uint8_t));
        } else {
            expectedWritten = sizeof(uint8_t);
        }

        ReadVolatileMemory(
            (const uint8_t*)SHMBuffer(&request->Metadata) + VIRTIO_BLK_STATUS_OFFSET,
            &requestStatus,
            sizeof(requestStatus)
        );

        if (requestStatus != VIRTIO_BLK_S_OK) {
            // __FinishRequest translates the device status into the storage
            // contract's error space and sends the one deferred response.
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
            // Submission published the chain but could not ring the device.
            // Reset/recovery owns completion of the request from this point.
            return VirtioBlkRecoverDevice(device);
        }
        
        if (oserr != OS_EOK) {
            __FinishRequest(request, oserr);
        }
    }
}
