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
 *
 *
 * MollenOS MCore - Virtio Driver
 * - Contains the implementation of a shared virtio driver
 *   for all the virtio devices
 * Follows the specification here:
 *   https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html
 */

#define __TRACE
#define __need_static_assert
#include "virtio-blk.h"

#include <ddk/barrier.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <event.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <io.h>
#include <ioctl.h>
#include <ioset.h>
#include <os/device.h>
#include <os/handle.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys_storage_service_client.h>
#include <ctt_storage_service_server.h>

#define VIRTIO_BLK_CONFIG_CAPACITY 0
#define VIRTIO_BLK_CONFIG_SIZE_MAX 8
#define VIRTIO_BLK_CONFIG_SEG_MAX  12
#define VIRTIO_BLK_CONFIG_BLK_SIZE 20

// Each direct request chain starts with a 16-byte device-readable header and
// ends with one device-writable status byte in the same metadata allocation.
#define VIRTIO_BLK_STATUS_OFFSET   sizeof(VirtioBlkRequestHeader_t)
#define VIRTIO_BLK_METADATA_SIZE   (VIRTIO_BLK_STATUS_OFFSET + sizeof(uint8_t))
#define VIRTIO_BLK_CONFIG_RETRIES  8
// The header and status each consume one descriptor in addition to data SGs.
#define VIRTIO_BLK_CHAIN_OVERHEAD  2
#define VIRTIO_BLK_SUPPORTED_FEATURES (VIRTIO_BLK_F_SIZE_MAX | \
    VIRTIO_BLK_F_SEG_MAX | VIRTIO_BLK_F_RO | VIRTIO_BLK_F_BLK_SIZE | \
    VIRTIO_F_RING_EVENT_IDX | VIRTIO_F_RING_RESET)

COMPILE_TIME_ASSERT(sizeof(VirtioBlkRequestHeader_t) == 16);

extern gracht_server_t* __crt_get_module_server(void);
extern int __crt_get_server_iod(void);

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

static oserr_t
__ReadCommonConfiguration(
    _In_  VirtioBlkDevice_t* device,
    _In_  uint32_t           offset,
    _In_  size_t             width,
    _Out_ uint64_t*          valueOut)
{
    return VirtioPciRegionRead(
            &device->Transport.Regions[VIRTIO_PCI_CAP_COMMON_CFG - 1],
            offset,
            width,
            valueOut
    );
}

static oserr_t
__ReadDeviceConfiguration(
    _In_  VirtioBlkDevice_t* device,
    _In_  uint32_t           offset,
    _In_  size_t             width,
    _Out_ uint64_t*          valueOut)
{
    VirtioPciRegion_t* region =
            &device->Transport.Regions[VIRTIO_PCI_CAP_DEVICE_CFG - 1];

    if (region->IoSpace == NULL) {
        return OS_ENOTSUPPORTED;
    }
    return VirtioPciRegionRead(region, offset, width, valueOut);
}

static oserr_t
__ReadStableDeviceConfiguration(
    _InOut_ VirtioBlkDevice_t* device)
{
    uint64_t capacity;
    uint64_t value;
    uint8_t  generationBefore;
    uint8_t  generationAfter;
    oserr_t  oserr;
    TRACE("__ReadStableDeviceConfiguration()");

    // Device-specific configuration can change asynchronously. A snapshot is
    // usable only when ConfigGeneration is unchanged across all field reads.
    for (int retry = 0; retry < VIRTIO_BLK_CONFIG_RETRIES; retry++) {
        oserr = __ReadCommonConfiguration(
            device,
            offsetof(VirtioPciCommonConfiguration_t, ConfigGeneration),
            sizeof(uint8_t),
            &value
        );
        if (oserr != OS_EOK) {
            ERROR("Failed to read the initial configuration generation: %u", oserr);
            return oserr;
        }
        generationBefore = (uint8_t)value;

        oserr = __ReadDeviceConfiguration(
            device,
            VIRTIO_BLK_CONFIG_CAPACITY,
            sizeof(capacity),
            &capacity
        );
        if (oserr != OS_EOK) {
            ERROR("Failed to read the block-device capacity: %u", oserr);
            return oserr;
        }

        // A zero size_max means that the device imposes no segment-size limit.
        device->SizeMax = UINT32_MAX;
        if (device->Features & VIRTIO_BLK_F_SIZE_MAX) {
            oserr = __ReadDeviceConfiguration(
                device,
                VIRTIO_BLK_CONFIG_SIZE_MAX,
                sizeof(uint32_t),
                &value
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to read size_max: %u", oserr);
                return oserr;
            }
            if ((uint32_t)value != 0) {
                device->SizeMax = (uint32_t)value;
            }
        }

        device->SegmentMax = UINT32_MAX;
        if (device->Features & VIRTIO_BLK_F_SEG_MAX) {
            oserr = __ReadDeviceConfiguration(
                device,
                VIRTIO_BLK_CONFIG_SEG_MAX,
                sizeof(uint32_t),
                &value
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to read seg_max: %u", oserr);
                return oserr;
            }
            device->SegmentMax = (uint32_t)value;
        }

        // Virtio addresses requests in fixed 512-byte sectors. blk_size only
        // changes the logical sector size exposed through Vali's storage API.
        device->Descriptor.SectorSize = VIRTIO_BLK_SECTOR_SIZE;
        if (device->Features & VIRTIO_BLK_F_BLK_SIZE) {
            oserr = __ReadDeviceConfiguration(
                device,
                VIRTIO_BLK_CONFIG_BLK_SIZE,
                sizeof(uint32_t),
                &value
            );
            if (oserr != OS_EOK) {
                ERROR("Failed to read blk_size: %u", oserr);
                return oserr;
            }
            device->Descriptor.SectorSize = (size_t)(uint32_t)value;
        }

        oserr = __ReadCommonConfiguration(
            device,
            offsetof(VirtioPciCommonConfiguration_t, ConfigGeneration),
            sizeof(uint8_t),
            &value
        );
        if (oserr != OS_EOK) {
            ERROR("Failed to read the final configuration generation: %u", oserr);
            return oserr;
        }
        generationAfter = (uint8_t)value;
        if (generationBefore == generationAfter) {
            // Vali currently exposes integral logical sectors. Reject block
            // sizes that cannot be represented as whole Virtio sectors.
            if (device->Descriptor.SectorSize < VIRTIO_BLK_SECTOR_SIZE ||
                (device->Descriptor.SectorSize % VIRTIO_BLK_SECTOR_SIZE) != 0) {
                return OS_ENOTSUPPORTED;
            }
            device->Descriptor.SectorCount =
                    capacity / (device->Descriptor.SectorSize / VIRTIO_BLK_SECTOR_SIZE);
            return device->Descriptor.SectorCount == 0 ? OS_ENOTSUPPORTED : OS_EOK;
        }
    }
    WARNING("Could not obtain a stable block-device configuration");
    return OS_EBUSY;
}

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

static oserr_t
__BuildIteration(
    _InOut_ VirtioBlkRequest_t* request,
    _Out_   VirtioQueueBuffer_t** buffersOut,
    _Out_   uint16_t*             bufferCountOut)
{
    VirtioBlkDevice_t* device = request->Device;
    VirtioQueueStats_t queueStats;
    VirtioQueueBuffer_t* buffers;
    VirtioBlkRequestHeader_t* header;
    size_t maximumDataDescriptors;
    size_t remainingBytes;
    size_t candidateBytes = 0;
    size_t iterationBytes;
    size_t dataOffset;
    size_t sgOffset;
    size_t dataDescriptorCount = 0;
    int sgIndex;
    oserr_t oserr;
        TRACE("__BuildIteration(transferred=%" PRIuIN "/%" PRIuIN ")",
            request->SectorsTransferred, request->SectorCount);

    *buffersOut = NULL;
    oserr = VirtioSplitQueueGetStats(device->RequestQueue, &queueStats);
    if (oserr != OS_EOK) {
        return oserr;
    }
    if (queueStats.FreeDescriptors <= VIRTIO_BLK_CHAIN_OVERHEAD ||
        device->SegmentMax == 0) {
        return OS_EBUSY;
    }

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

    for (int index = sgIndex;
         remainingBytes > 0 &&
             dataDescriptorCount < maximumDataDescriptors &&
             index < request->DataSg.Count;
         index++) {
        size_t available = request->DataSg.Entries[index].Length - sgOffset;
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
            request->MetadataSg.Entries[0].Address);
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
    uint16_t bufferCount;
    oserr_t oserr;

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
    // failed. Ownership has still transferred to the queue, so the request must
    // remain alive for completion or reset recovery.
    return oserr == OS_EINPROGRESS ? OS_EOK : oserr;
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

    if (oserr == OS_EOK) {
        if (status == VIRTIO_BLK_S_IOERR) {
            oserr = OS_EDEVFAULT;
        }
        else if (status == VIRTIO_BLK_S_UNSUPP) {
            oserr = OS_ENOTSUPPORTED;
        }
        else if (status != VIRTIO_BLK_S_OK) {
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
VirtioBlkDeviceRegisterStorage(
    _In_ VirtioBlkDevice_t* device)
{
    struct vali_link_message message;
    int status;

    if (device == NULL || device->Registered) {
        return OS_EINVALPARAMS;
    }
    TRACE("VirtioBlkDeviceRegisterStorage(device=%u)", device->Descriptor.DeviceID);
    message = (struct vali_link_message)VALI_MSG_INIT_HANDLE(GetFileService());

    // Registration occurs only after DRIVER_OK. filed immediately queries the
    // descriptor and may start partition I/O once this message is delivered.
    // The caller must therefore insert the device into its lookup collection
    // before invoking this function.
    status = sys_storage_register(
        GetGrachtClient(),
        &message.base,
        GetNativeHandle(__crt_get_server_iod()),
        device->Descriptor.DeviceID,
        (enum sys_storage_flags)device->Descriptor.Flags
    );

    if (status == 0) {
        device->Registered = 1;
        return OS_EOK;
    }
    ERROR("Failed to register Virtio storage device %u", device->Descriptor.DeviceID);
    return OS_EUNKNOWN;
}

static void
__UnregisterStorage(
    _In_ VirtioBlkDevice_t* device)
{
    struct vali_link_message message = VALI_MSG_INIT_HANDLE(GetFileService());

    if (device->Registered) {
        (void)sys_storage_unregister(
            GetGrachtClient(),
            &message.base,
            device->Descriptor.DeviceID,
            1
        );
        device->Registered = 0;
    }
}

static oserr_t
__ResetRequestQueue(
    _InOut_ VirtioBlkDevice_t* device)
{
    VirtioQueueCompletion_t completion;
    oserr_t oserr;
    TRACE("__ResetRequestQueue()");

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

static oserr_t
__RecoverDevice(
    _InOut_ VirtioBlkDevice_t* device)
{
    oserr_t oserr;
    TRACE("__RecoverDevice()");

    oserr = __ResetRequestQueue(device);

    if (oserr == OS_EOK) {
        oserr = VirtioPciNegotiateFeatures(
            &device->Transport,
            VIRTIO_BLK_SUPPORTED_FEATURES,
            VIRTIO_F_VERSION_1,
            &device->Features
        );
    }
    if (oserr == OS_EOK) {
        oserr = __ReadStableDeviceConfiguration(device);
    }
    if (oserr == OS_EOK) {
        device->Descriptor.Flags &= ~STORAGE_READONLY;
        if (device->Features & VIRTIO_BLK_F_RO) {
            device->Descriptor.Flags |= STORAGE_READONLY;
        }
        oserr = VirtioSplitQueueCreate(
            &device->Transport,
            0,
            VIRTIO_BLK_QUEUE_SIZE,
            &device->RequestQueue
        );
    }
    if (oserr == OS_EOK) {
        oserr = VirtioPciFinishInitialization(&device->Transport);
    }
    if (oserr != OS_EOK) {
        ERROR("Virtio block device recovery failed: %u", oserr);
        VirtioPciSetFailed(&device->Transport);
        __UnregisterStorage(device);
    }
    return oserr;
}

static oserr_t
__RegisterInterrupt(
    _InOut_ VirtioBlkDevice_t* device)
{
    DeviceInterrupt_t interrupt;
    DeviceIo_t* isrIoSpace =
            device->Transport.Regions[VIRTIO_PCI_CAP_ISR_CFG - 1].IoSpace;
    int enable = 1;
    TRACE("__RegisterInterrupt()");

    // The fast handler only acknowledges the ISR and signals this descriptor.
    // Queue draining and protocol responses stay in the normal module context.
    device->EventDescriptor = eventd(0, EVT_RESET_EVENT);
    if (device->EventDescriptor < 0) {
        ERROR("Failed to create Virtio block interrupt event descriptor");
        return OS_EUNKNOWN;
    }
    if (ioset_ctrl(
            gracht_server_get_aio_handle(__crt_get_module_server()),
            IOSET_ADD,
            device->EventDescriptor,
            &(struct ioset_event) {
                .data.context = device,
                .events = IOSETSYN
            }
        ) != 0) {
        ERROR("Failed to register Virtio block interrupt event descriptor");
        close(device->EventDescriptor);
        device->EventDescriptor = -1;
        return OS_EUNKNOWN;
    }
    (void)ioctl(device->EventDescriptor, FIONBIO, &enable);

    device->InterruptResource.IsrOffset =
            device->Transport.Regions[VIRTIO_PCI_CAP_ISR_CFG - 1].Offset;
    atomic_init(&device->InterruptResource.PendingStatus, 0);
    DeviceInterruptInitialize(&interrupt, device->BusDevice);
        TRACE("__RegisterInterrupt line=%i, pin=%i, isrOffset=0x%x",
            interrupt.Line,
            interrupt.Pin,
            device->InterruptResource.IsrOffset);
    RegisterInterruptDescriptor(&interrupt, device->EventDescriptor);
    RegisterFastInterruptHandler(&interrupt, (InterruptHandler_t)OnFastInterrupt);
    RegisterFastInterruptIoResource(&interrupt, isrIoSpace);
    RegisterFastInterruptMemoryResource(
        &interrupt,
        (uintptr_t)&device->InterruptResource,
        sizeof(device->InterruptResource),
        0
    );
    device->InterruptId = RegisterInterruptSource(&interrupt, 0);
    if (device->InterruptId == UUID_INVALID) {
        ERROR("Failed to register the Virtio block interrupt source");
        ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
                   IOSET_DEL, device->EventDescriptor, NULL);
        close(device->EventDescriptor);
        device->EventDescriptor = -1;
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

VirtioBlkDevice_t*
VirtioBlkDeviceCreate(
    _In_ BusDevice_t* busDevice,
    _In_ uuid_t       storageDeviceId)
{
    VirtioBlkDevice_t* device;
    oserr_t oserr;
    TRACE("VirtioBlkDeviceCreate(storageDeviceId=%u)", storageDeviceId);

    if (busDevice == NULL) {
        return NULL;
    }
    device = calloc(1, sizeof(VirtioBlkDevice_t));
    if (device == NULL) {
        free(busDevice);
        return NULL;
    }
    ELEMENT_INIT(&device->Header, (void*)(uintptr_t)busDevice->Base.Id, device);
    device->BusDevice = busDevice;
    device->InterruptId = UUID_INVALID;
    device->EventDescriptor = -1;
    device->Descriptor.DeviceID = storageDeviceId;
    device->Descriptor.DriverID = GetNativeHandle(__crt_get_server_iod());
    device->Descriptor.LUNCount = 1;
    memcpy(device->Descriptor.Model, "Virtio Block Device", 20);
    snprintf(
        device->Descriptor.Serial,
        sizeof(device->Descriptor.Serial),
        "virtio-%u",
        storageDeviceId
    );

    oserr = VirtioPciTransportInitialize(busDevice, &device->Transport);
    if (oserr == OS_EOK) {
        oserr = VirtioPciNegotiateFeatures(
            &device->Transport,
            VIRTIO_BLK_SUPPORTED_FEATURES,
            VIRTIO_F_VERSION_1,
            &device->Features
        );
    }
    if (oserr == OS_EOK) {
        oserr = __ReadStableDeviceConfiguration(device);
    }
    if (oserr == OS_EOK) {
        if (device->Features & VIRTIO_BLK_F_RO) {
            device->Descriptor.Flags |= STORAGE_READONLY;
        }
        oserr = VirtioSplitQueueCreate(
            &device->Transport,
            0,
            VIRTIO_BLK_QUEUE_SIZE,
            &device->RequestQueue
        );
    }
    if (oserr == OS_EOK) {
        oserr = __RegisterInterrupt(device);
    }
    if (oserr == OS_EOK) {
        oserr = VirtioPciFinishInitialization(&device->Transport);
    }
    if (oserr != OS_EOK) {
        ERROR("Failed to initialize Virtio block device: %u", oserr);
        VirtioPciSetFailed(&device->Transport);
        VirtioBlkDeviceDestroy(device);
        return NULL;
    }
    return device;
}

void
VirtioBlkDeviceDestroy(
    _In_ VirtioBlkDevice_t* device)
{
    oserr_t oserr;

    if (device == NULL) {
        return;
    }
    TRACE("VirtioBlkDeviceDestroy(device=%u)", device->Descriptor.DeviceID);

    // Stop externally visible I/O before tearing down interrupts and queue DMA.
    __UnregisterStorage(device);
    if (device->InterruptId != UUID_INVALID) {
        UnregisterInterruptSource(device->InterruptId);
    }
    if (device->EventDescriptor >= 0) {
        ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
                   IOSET_DEL, device->EventDescriptor, NULL);
        close(device->EventDescriptor);
    }

    if (device->Transport.Device != NULL) {
        oserr = __ResetRequestQueue(device);
        if (oserr != OS_EOK) {
            // Freeing queue or transport memory while reset failed would leave
            // the device able to DMA into released memory. Leak deliberately
            // rather than turn a teardown failure into memory corruption.
            ERROR("Refusing unsafe Virtio block teardown after reset failure: %u", oserr);
            return;
        }
    }
    VirtioPciTransportDestroy(&device->Transport);
    free(device->BusDevice);
    free(device);
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
    size_t byteCount;
    oserr_t oserr;
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
    oserr_t oserr;

    if (device == NULL) {
        return OS_EINVALPARAMS;
    }
    for (;;) {
        oserr = VirtioSplitQueuePoll(device->RequestQueue, &completion);
        if (oserr == OS_ENOENT) {
            return OS_EOK;
        }
        if (oserr != OS_EOK) {
            return oserr;
        }

        // VirtioSplitQueuePoll orders all device writes before returning the
        // used entry. Read the device-written request status only afterwards.
        VirtioBlkRequest_t* request = completion.Context;
        uint8_t requestStatus;
        size_t iterationBytes = request->IterationSectors *
            device->Descriptor.SectorSize;
        uint32_t expectedWritten = request->Direction == __STORAGE_OPERATION_READ ?
            (uint32_t)(iterationBytes + sizeof(uint8_t)) : sizeof(uint8_t);

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
        if (oserr != OS_EOK) {
            __FinishRequest(request, oserr);
        }
    }
}

oserr_t
VirtioBlkDeviceHandleConfigurationChange(
    _InOut_ VirtioBlkDevice_t* device)
{
    uint64_t value;
    uint8_t  status;
    oserr_t  oserr;

    if (device == NULL) {
        return OS_EINVALPARAMS;
    }
    oserr = __ReadCommonConfiguration(
        device,
        offsetof(VirtioPciCommonConfiguration_t, DeviceStatus),
        sizeof(uint8_t),
        &value
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    status = (uint8_t)value;
    if (status & VIRTIO_STATUS_DEVICE_NEEDS_RESET) {
        // Recovery cancels in-flight requests, renegotiates features, rebuilds
        // the queue, and only then returns the device to DRIVER_OK.
        return __RecoverDevice(device);
    }
    if (status & VIRTIO_STATUS_FAILED) {
        __UnregisterStorage(device);
        return OS_EDEVFAULT;
    }
    return __ReadStableDeviceConfiguration(device);
}
