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
 */

#include <os/handle.h>
#include <os/memory.h>
#include <os/shm.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#include "hci.h"
#include "transfer.h"

static oserr_t
__SelectStaging(
    _InOut_ UsbManagerTransfer_t* transfer,
    _In_    size_t                offset,
    _In_    size_t                pageSize,
    _In_    enum DMADirection     direction)
{
    struct UsbTransferDmaStaging* staging = &transfer->Dma.Staging;
    SHM_t                         options = {
        .Flags = SHM_COMMIT | SHM_CLEAN | SHM_PRIVATE,
        .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
        .Size = pageSize
    };

    unsigned int access = direction == DMAFromDevice ? SHM_ACCESS_WRITE : SHM_ACCESS_READ;
    oserr_t      status;

    if (staging->Buffer.ID == UUID_INVALID) {
        // Map the client ring once from its origin. The attachment keeps this
        // view; packet copies use explicit byte offsets and never remap it.
        status = SHMMap(
            &transfer->SHMHandle,
            0,
            transfer->Dma.BufferOffset + transfer->Dma.BufferLength,
            access
        );
        if (status != OS_EOK) {
            return status;
        }

        status = SHMCreate(&options, &staging->Buffer);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (transfer->Dma.Mapping == NULL) {
        // The whole private page belongs to this mapping, including the last
        // cache line of a short packet. An unreachable page may still bounce,
        // but at most one bounce allocation is kept for this transfer.
        status = DMAMapSHM(
            transfer->Dma.Context,
            &staging->Buffer,
            0,
            pageSize,
            direction,
            &transfer->Dma.Mapping
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    
    staging->ClientOffset = offset;
    if (direction == DMAToDevice) {
        memcpy(
            SHMBuffer(&staging->Buffer),
            (const uint8_t*)SHMBuffer(&transfer->SHMHandle) + offset,
            transfer->Dma.Length
        );
    }
    // Creation already saved the immutable list; rearm performs no query.
    transfer->Dma.Segments = DMAMappingSegments(
        transfer->Dma.Mapping,
        &transfer->Dma.SegmentCount
    );
    return OS_EOK;
}

static oserr_t
__SelectMapping(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    struct DMAMapping** mapping = &transfer->Dma.Mapping;
    enum DMADirection   direction;
    size_t              offset = transfer->Dma.BufferOffset;
    size_t              capacity = SHMBufferCapacity(&transfer->SHMHandle);
    size_t              pageSize;
    size_t              bytes;
    uint32_t            packet;
    uint32_t            index;
    uint32_t            count;
    oserr_t             status;

    if (transfer->Dma.DeviceOwned || transfer->Dma.HardwareActive) {
        return OS_EBUSY;
    }
    if (offset > capacity || transfer->Dma.BufferLength > capacity - offset) {
        return OS_EINVALPARAMS;
    }
    if (transfer->Dma.BufferLength == 0) {
        return OS_EINVALPARAMS;
    }

    transfer->Dma.Length = transfer->Dma.BufferLength;
    direction = transfer->Direction == USBTRANSFER_DIRECTION_IN ?
        DMAFromDevice : DMAToDevice;
    
    if (__Transfer_IsPeriodic(transfer)) {
        packet = transfer->MaxPacketSize * (transfer->TData.Periodic.Bandwith + 1);
        index = transfer->TData.Periodic.CurrentDataIndex;
        if (packet == 0 || index >= transfer->Dma.BufferLength) {
            return OS_EINVALPARAMS;
        }
        if (index % packet != 0) {
            return OS_EINVALPARAMS;
        }

        offset += index;
        transfer->Dma.Length = transfer->Dma.BufferLength - index;
        if (transfer->Dma.Length > packet) {
            transfer->Dma.Length = packet;
        }
        pageSize = MemoryPageSize();
        if (packet <= pageSize) {
            return __SelectStaging(transfer, offset, pageSize, direction);
        }

        // One entry per packet slot bounds the cache by the submitted buffer.
        // Only visited slots acquire mappings and possible bounce storage.
        if (transfer->Dma.Mappings == NULL) {
            count = 1 + (transfer->Dma.BufferLength - 1) / packet;
            bytes = (size_t)count * sizeof(*transfer->Dma.Mappings);
            if (bytes / sizeof(*transfer->Dma.Mappings) != count) {
                return OS_EOOM;
            }
            transfer->Dma.Mappings = calloc(count, sizeof(*transfer->Dma.Mappings));
            if (transfer->Dma.Mappings == NULL) {
                return OS_EOOM;
            }
            transfer->Dma.MappingCount = count;
        }
        mapping = &transfer->Dma.Mappings[index / packet];
    }

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        // Setup is inline in the TRB. Capture it before handing the mapping to
        // the device, including when the payload uses bounce storage.
        if (transfer->Dma.Length < sizeof(usb_packet_t)) {
            return OS_EINVALPARAMS;
        }
        // SHM mappings are page based. Map from the buffer origin so the byte
        // offset is explicit, including for setup packets inside a shared pool.
        status = SHMMap(&transfer->SHMHandle, 0, offset + sizeof(usb_packet_t), SHM_ACCESS_READ);
        if (status != OS_EOK) {
            return status;
        }
        memcpy(&transfer->Dma.Setup,
            (const uint8_t*)SHMBuffer(&transfer->SHMHandle) + offset, sizeof(usb_packet_t));
        direction = DMABidirectional;
    }
    if (*mapping == NULL) {
        status = DMAMapSHM(
            transfer->Dma.Context,
            &transfer->SHMHandle,
            offset,
            transfer->Dma.Length,
            direction,
            mapping
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    transfer->Dma.Mapping = *mapping;
    // Creation already saved the immutable list; rearm performs no query.
    transfer->Dma.Segments = DMAMappingSegments(
        transfer->Dma.Mapping,
        &transfer->Dma.SegmentCount
    );
    return OS_EOK;
}

oserr_t
USBTransferDmaPrepare(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    struct TransferElement* elements;
    struct UsbTransferDmaLayout* layout = &transfer->Dma.Layout;
    int                     count;
    oserr_t                 status;

    status = __SelectMapping(transfer);
    if (status != OS_EOK) {
        return status;
    }

    // Selection still refreshes OUT staging bytes and the client's slot offset.
    // Only the immutable device layout is reused, never the ownership handoff.
    if (__Transfer_IsPeriodic(transfer) && layout->Mapping == transfer->Dma.Mapping &&
        layout->Length == transfer->Dma.Length) {
        if (layout->MaxPacketSize == transfer->MaxPacketSize &&
            layout->Type == transfer->Type && layout->Direction == transfer->Direction) {
            return OS_EOK;
        }
    }

    // DMA controllers consume the immutable device segments directly.
    status = HCITransferElementsNeeded(
        transfer,
        transfer->Dma.Length,
        transfer->Direction,
        NULL,
        0,
        &count
    );
    if (status != OS_EOK) {
        return status;
    }
    if (count > transfer->ElementCapacity) {
        if ((size_t)count > SIZE_MAX / sizeof(*elements)) {
            return OS_EOOM;
        }
        elements = realloc(transfer->Elements, count * sizeof(*elements));
        if (elements == NULL) {
            return OS_EOOM;
        }
        transfer->Elements = elements;
        transfer->ElementCapacity = count;
    }

    transfer->ElementCount = count;
    memset(transfer->Elements, 0, count * sizeof(*transfer->Elements));
    HCITransferElementFill(
        transfer,
        transfer->Dma.Length,
        transfer->Direction,
        NULL,
        0
    );
    layout->Mapping = transfer->Dma.Mapping;
    layout->Length = transfer->Dma.Length;
    layout->MaxPacketSize = transfer->MaxPacketSize;
    layout->Type = transfer->Type;
    layout->Direction = transfer->Direction;
    return OS_EOK;
}

oserr_t
USBTransferDmaDestroy(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    struct DMAMapping* mapping;
    uint32_t           i;
    oserr_t            status;

    if (transfer->Dma.DeviceOwned || transfer->Dma.HardwareActive) {
        return OS_EBUSY;
    }
    // A released mapping's address can be reused by a subsequent allocation.
    memset(&transfer->Dma.Layout, 0, sizeof(transfer->Dma.Layout));
    if (transfer->Dma.Mappings == NULL) {
        status = DMAUnmap(&transfer->Dma.Mapping);
        if (status != OS_EOK) {
            return status;
        }
    } else {
        for (i = 0; i < transfer->Dma.MappingCount; i++) {
            mapping = transfer->Dma.Mappings[i];
            status = DMAUnmap(&transfer->Dma.Mappings[i]);
            if (status != OS_EOK) {
                return status;
            }
            if (mapping == transfer->Dma.Mapping) {
                transfer->Dma.Mapping = NULL;
                transfer->Dma.Segments = NULL;
                transfer->Dma.SegmentCount = 0;
            }
        }
        free(transfer->Dma.Mappings);
        transfer->Dma.Mappings = NULL;
        transfer->Dma.MappingCount = 0;
    }
    // The streaming mapping must be gone before releasing its backing SHM.
    // Keep both owners intact when unmap fails so destruction can be retried.
    if (transfer->Dma.Staging.Buffer.ID != UUID_INVALID) {
        status = SHMRelease(&transfer->Dma.Staging.Buffer);
        if (status != OS_EOK) {
            WARNING("USB DMA staging release failed (%u); keeping transfer", status);
            return status;
        }
        memset(&transfer->Dma.Staging, 0, sizeof(transfer->Dma.Staging));
    }
    transfer->Dma.Segments = NULL;
    transfer->Dma.SegmentCount = 0;
    return OS_EOK;
}

oserr_t
USBTransferDmaComplete(
    _InOut_ UsbManagerTransfer_t* transfer,
    _In_    size_t                bytes)
{
    oserr_t status;

    if (!transfer->Dma.DeviceOwned) {
        transfer->Dma.HardwareActive = false;
        return OS_EOK;
    }
    if (bytes > transfer->Dma.Length) {
        return OS_EINVALPARAMS;
    }
    status = DMAEndTransfer(transfer->Dma.Mapping, bytes);
    if (status == OS_EOK) {
        transfer->Dma.DeviceOwned = false;
        transfer->Dma.HardwareActive = false;
        if (transfer->Dma.Staging.Buffer.ID != UUID_INVALID &&
            transfer->Direction == USBTRANSFER_DIRECTION_IN && bytes != 0) {
            // Publish only completed bytes. Cancellation (zero bytes) and a
            // short receive must leave the rest of the client's slot intact.
            memcpy(
                (uint8_t*)SHMBuffer(&transfer->SHMHandle) + transfer->Dma.Staging.ClientOffset,
                SHMBuffer(&transfer->Dma.Staging.Buffer),
                bytes
            );
        }
    }
    return status;
}
