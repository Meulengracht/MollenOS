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

#define __TRACE
#include <os/handle.h>
#include <os/memory.h>
#include <os/shm.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <string.h>

#include "../hci.h"
#include "transfer.h"

static oserr_t
__SelectBounce(
    _InOut_ UsbManagerTransfer_t* transfer,
    _In_    size_t                offset,
    _In_    size_t                pageSize,
    _In_    enum DMADirection     direction)
{
    struct UsbTransferDmaBounce* bounce = &transfer->Dma.Bounce;
    SHM_t                         options = {
        .Flags = SHM_COMMIT | SHM_CLEAN | SHM_PRIVATE,
        .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE,
        .Size = pageSize
    };

    unsigned int access = direction == DMAFromDevice ? SHM_ACCESS_READ | SHM_ACCESS_WRITE : SHM_ACCESS_READ;
    oserr_t      status;

    if (bounce->Buffer.ID == UUID_INVALID) {
        // Map the client ring from its start once. Packet copies use byte
        // offsets into this mapping, so moving to another slot needs no remap.
        status = SHMMap(
            &transfer->SHMHandle,
            0,
            transfer->Dma.BufferOffset + transfer->Dma.BufferLength,
            access
        );
        if (status != OS_EOK) {
            return status;
        }

        status = SHMCreate(&options, &bounce->Buffer);
        if (status != OS_EOK) {
            return status;
        }
    }

    if (transfer->Dma.Mapping == NULL) {
        // Map the whole private page to include the last cache line of a short
        // packet. If the device cannot reach the page, reuse one bounce buffer
        // for this transfer.
        status = DMAMapSHM(
            transfer->Dma.Context,
            &bounce->Buffer,
            0,
            pageSize,
            direction,
            NULL,
            &transfer->Dma.Mapping
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    
    bounce->ClientOffset = offset;
    if (direction == DMAToDevice) {
        memcpy(
            SHMBuffer(&bounce->Buffer),
            (const uint8_t*)SHMBuffer(&transfer->SHMHandle) + offset,
            transfer->Dma.Length
        );
    }
    // Use the fixed segment list saved when the mapping was created.
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
            return __SelectBounce(transfer, offset, pageSize, direction);
        }

        // Reserve one entry per packet slot in the submitted buffer. Create
        // mappings and any bounce buffers as each slot is used.
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
        // The TRB contains the setup packet. Copy it before the device can
        // access the mapping, including when the payload uses a bounce buffer.
        if (transfer->Dma.Length < sizeof(usb_packet_t)) {
            return OS_EINVALPARAMS;
        }
        // SHM mappings start on page boundaries. Map from the buffer start and
        // add the byte offset when copying a setup packet from a shared pool.
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
            NULL,
            mapping
        );
        if (status != OS_EOK) {
            return status;
        }
    }
    transfer->Dma.Mapping = *mapping;
    // Use the fixed segment list saved when the mapping was created.
    transfer->Dma.Segments = DMAMappingSegments(
        transfer->Dma.Mapping,
        &transfer->Dma.SegmentCount
    );
    return OS_EOK;
}

oserr_t
UsbTransferDmaPrepare(
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

    // Select the mapping first to copy OUT data into bounce and update the
    // client's slot offset. Reusing the layout still requires a DMA begin call.
    if (__Transfer_IsPeriodic(transfer) && layout->Mapping == transfer->Dma.Mapping &&
        layout->Length == transfer->Dma.Length) {
        if (layout->MaxPacketSize == transfer->MaxPacketSize &&
            layout->Type == transfer->Type && layout->Direction == transfer->Direction) {
            return OS_EOK;
        }
    }

    // DMA controllers use the mapping's device segments directly.
    status = HCITransferElementsNeeded(
        transfer,
        transfer->Dma.Length,
        transfer->Direction,
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
        transfer->Direction
    );
    status = UsbTransferBounceInitialize(transfer);
    if (status != OS_EOK) {
        return status;
    }
    layout->Mapping = transfer->Dma.Mapping;
    layout->Length = transfer->Dma.Length;
    layout->MaxPacketSize = transfer->MaxPacketSize;
    layout->Type = transfer->Type;
    layout->Direction = transfer->Direction;
    return OS_EOK;
}

oserr_t
UsbTransferDmaBegin(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    oserr_t status;

    // If a request needs several descriptor-pool passes, begin DMA once for
    // the shared mapping. The CPU can access the full completed prefix after the
    // final pass completes.
    if (transfer->Dma.DeviceOwned) {
        return OS_EOK;
    }

    status = UsbTransferBouncePrepare(transfer);
    if (status != OS_EOK) {
        (void)UsbTransferBounceAbort(transfer);
        return status;
    }
    status = DMABeginTransfer(
        transfer->Dma.Mapping,
        0,
        transfer->Dma.Length,
        SHMWaitNonBlocking,
        NULL
    );
    if (status == OS_EOK) {
        transfer->Dma.DeviceOwned = true;
        transfer->Dma.Completed = 0;
    } else {
        // No hardware can reference these slots yet. The next attempt must
        // copy OUT bytes again after CPU access becomes available.
        (void)UsbTransferBounceAbort(transfer);
    }
    return status;
}

oserr_t
UsbTransferDmaDestroy(
    _InOut_ UsbManagerTransfer_t* transfer)
{
    struct DMAMapping* mapping;
    uint32_t           i;
    oserr_t            status;

    if (transfer->Dma.DeviceOwned || transfer->Dma.HardwareActive) {
        return OS_EBUSY;
    }
    status = UsbTransferBounceDestroy(transfer);
    if (status != OS_EOK) {
        return status;
    }
    // Clear the layout before freeing mappings, as a new mapping may get the
    // same address.
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
    // Unmap DMA before releasing the SHM buffer. If unmapping fails, leave
    // the mapping and buffer in place so destruction can be retried.
    if (transfer->Dma.Bounce.Buffer.ID != UUID_INVALID) {
        status = SHMRelease(&transfer->Dma.Bounce.Buffer);
        if (status != OS_EOK) {
            WARNING("USB DMA bounce release failed (%u); keeping transfer", status);
            return status;
        }
        memset(&transfer->Dma.Bounce, 0, sizeof(transfer->Dma.Bounce));
    }
    transfer->Dma.Segments = NULL;
    transfer->Dma.SegmentCount = 0;
    return OS_EOK;
}

oserr_t
UsbTransferDmaComplete(
    _InOut_ UsbManagerTransfer_t* transfer,
    _In_    size_t                bytes)
{
    oserr_t status;

    if (!transfer->Dma.DeviceOwned) {
        transfer->Dma.HardwareActive = false;
        // DMA end may have succeeded while packet copy-back was blocked. Retry
        // only the CPU copy/end step, before the manager notifies the client.
        return UsbTransferBounceComplete(transfer, bytes);
    }
    if (bytes > transfer->Dma.Length) {
        return OS_EINVALPARAMS;
    }
    status = DMAEndTransfer(transfer->Dma.Mapping, bytes);
    if (transfer->Dma.Diagnostics.Enabled && transfer->Dma.Diagnostics.Completions < 3) {
        transfer->Dma.Diagnostics.Completions++;
        TRACE("USB EP0 DMA end: transfer=%u status=%u bytes=%u length=%u",
            transfer->ID, status, (unsigned int)bytes, transfer->Dma.Length);
    }
    if (status == OS_EOK) {
        transfer->Dma.DeviceOwned = false;
        transfer->Dma.HardwareActive = false;
        status = UsbTransferBounceComplete(transfer, bytes);
        if (status != OS_EOK) {
            return status;
        }
        if (transfer->Dma.Bounce.Buffer.ID != UUID_INVALID &&
            transfer->Direction == USBTRANSFER_DIRECTION_IN && bytes != 0) {
            // Copy only completed bytes. Cancellation (zero bytes) and a
            // short receive must leave the rest of the client's slot intact.
            memcpy(
                (uint8_t*)SHMBuffer(&transfer->SHMHandle) + transfer->Dma.Bounce.ClientOffset,
                SHMBuffer(&transfer->Dma.Bounce.Buffer),
                bytes
            );
        }
    }
    return status;
}
