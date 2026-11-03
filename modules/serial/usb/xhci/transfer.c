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

#define __need_minmax
#include <ddk/barrier.h>
#include <ddk/io.h>
#include <os/shm.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "xhci.h"

static int
__TransferElementsRemaining(
    _In_ UsbManagerTransfer_t* transfer)
{
    if (__Transfer_IsAsync(transfer)) {
        return transfer->ElementCount - transfer->TData.Async.ElementsCompleted;
    }
    return transfer->ElementCount;
}

static void
__BuildDescriptor(
    _In_ XhciTransferDescriptor_t* descriptor,
    _In_ UsbManagerTransfer_t*     transfer,
    _In_ XhciEndpoint_t*           endpoint,
    _In_ uint16_t                  trbIndex)
{
    // Before reusing a descriptor, clear its submission state. Keep the
    // scheduler fields that identify its allocation.
    descriptor->Object.DepthIndex = USB_ELEMENT_NO_INDEX;
    descriptor->Object.BreathIndex = USB_ELEMENT_NO_INDEX;
    descriptor->Object.Flags &= ~USB_ELEMENT_PROCESSED;
    descriptor->Flags = 0;
    descriptor->CompletionCode = 0;
    descriptor->BytesTransferred = 0;
    descriptor->TransferLength = 0;
    descriptor->CacheNext = NULL;
    memset(&descriptor->QueueHeader, 0, sizeof(descriptor->QueueHeader));
    descriptor->Transfer      = transfer;
    descriptor->Endpoint      = endpoint;
    descriptor->FirstTrbIndex = trbIndex;
    descriptor->LastTrbIndex  = trbIndex;
    descriptor->TrbCount      = 1;
    descriptor->CycleState    = endpoint->TransferRing.CycleState;
}

static XhciTransferDescriptor_t*
__DescriptorNext(
    _In_ XhciController_t*          controller,
    _In_ XhciTransferDescriptor_t* descriptor)
{
    uint16_t nextIndex = descriptor->Object.DepthIndex;

    if (nextIndex == USB_ELEMENT_NO_INDEX) {
        return NULL;
    }
    return (XhciTransferDescriptor_t*)USB_ELEMENT_INDEX(
            USB_ELEMENT_GET_POOL(controller->Base.Scheduler, nextIndex),
            nextIndex
    );
}

static void
__BuildTrb(
    _In_  UsbManagerTransfer_t* transfer,
    _In_  int                   elementIndex,
    _Out_ XhciTrb_t*            trbOut)
{
    struct TransferElement* element = &transfer->Elements[elementIndex];
    bool isLast = elementIndex == transfer->ElementCount - 1;

    memset(trbOut, 0, sizeof(XhciTrb_t));
    trbOut->Parameter = element->Data.Address;
    trbOut->Status = element->Length;

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (elementIndex == 0) {
            memcpy(&trbOut->Parameter, &transfer->Dma.Setup, sizeof(usb_packet_t));
            trbOut->Status = sizeof(usb_packet_t);
            trbOut->Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_SETUP_STAGE) |
                    XHCI_TRB_CONTROL_IDT | XHCI_TRB_CONTROL_CHAIN;
            if (transfer->ElementCount > 2) {
                trbOut->Control |= transfer->Direction == USBTRANSFER_DIRECTION_IN ?
                        XHCI_TRB_CONTROL_TRT_IN : XHCI_TRB_CONTROL_TRT_OUT;
            } else {
                trbOut->Control |= XHCI_TRB_CONTROL_TRT_NONE;
            }
            return;
        }

        if (isLast) {
            trbOut->Parameter = 0;
            trbOut->Status = 0;
            trbOut->Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_STATUS_STAGE) |
                    XHCI_TRB_CONTROL_IOC;
            if (element->Type == TRANSFERELEMENT_TYPE_IN) {
                trbOut->Control |= XHCI_TRB_CONTROL_DIR_IN;
            }
            return;
        }

        trbOut->Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_DATA_STAGE) |
                XHCI_TRB_CONTROL_CHAIN | XHCI_TRB_CONTROL_ISP;
        if (element->Type == TRANSFERELEMENT_TYPE_IN) {
            trbOut->Control |= XHCI_TRB_CONTROL_DIR_IN;
        }
        return;
    }

    trbOut->Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_NORMAL) |
            XHCI_TRB_CONTROL_ISP | (isLast ? XHCI_TRB_CONTROL_IOC : XHCI_TRB_CONTROL_CHAIN);
}

/** Walk device segments in order, using the same splits for counting and filling. */
static oserr_t
__BuildTransferElements(
    _In_  UsbManagerTransfer_t*    transfer,
    _In_  uint32_t                 length,
    _Out_ struct TransferElement* elements,
    _Out_ int*                    countOut)
{
    const struct DMASegment* segment;
    enum TransferElementType      type = __TransferElement_DirectionToType(transfer->Direction);
    enum TransferElementType      ackType = TRANSFERELEMENT_TYPE_IN;
    uint32_t                      remaining = length;
    uint32_t                      segmentIndex = 0;
    uint64_t                      offset = 0;
    uint64_t                      address;
    uint32_t                      chunk;
    int                           count = 0;

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (remaining < sizeof(usb_packet_t)) {
            return OS_EINVALPARAMS;
        }
        remaining -= sizeof(usb_packet_t);
        offset = sizeof(usb_packet_t);
        if (elements != NULL) {
            elements[count].Type = TRANSFERELEMENT_TYPE_SETUP;
            elements[count].Length = sizeof(usb_packet_t);
        }
        count++;
        if (remaining != 0 && transfer->Direction == USBTRANSFER_DIRECTION_IN) {
            ackType = TRANSFERELEMENT_TYPE_OUT;
        }
    }

    while (remaining != 0) {
        while (segmentIndex < transfer->Dma.SegmentCount &&
            offset >= transfer->Dma.Segments[segmentIndex].Length) {
            offset -= transfer->Dma.Segments[segmentIndex].Length;
            segmentIndex++;
        }
        if (segmentIndex == transfer->Dma.SegmentCount) {
            return OS_EINVALPARAMS;
        }
        segment = &transfer->Dma.Segments[segmentIndex];
        address = segment->Address + offset;
        chunk = (uint32_t)MIN(remaining, segment->Length - offset);
        chunk = MIN(chunk, 0x10000 - (address & 0xffff));
        if (count == INT_MAX) {
            return OS_EOVERFLOW;
        }
        if (elements != NULL) {
            elements[count].Type = type;
            elements[count].Data.Address = (uintptr_t)address;
            elements[count].Length = chunk;
        }
        count++;
        offset += chunk;
        remaining -= chunk;

        // Add a final zero-length element for isochronous transfers and for
        // OUT transfers whose last chunk matches the maximum packet size.
        if (remaining == 0 && (transfer->Type == USBTRANSFER_TYPE_ISOC ||
            (transfer->Direction == USBTRANSFER_DIRECTION_OUT && chunk == transfer->MaxPacketSize))) {
            if (count == INT_MAX) {
                return OS_EOVERFLOW;
            }
            if (elements != NULL) {
                elements[count].Type = type;
            }
            count++;
        }
    }
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        if (count == INT_MAX) {
            return OS_EOVERFLOW;
        }
        if (elements != NULL) {
            elements[count].Type = ackType;
        }
        count++;
    }
    *countOut = count;
    return count == 0 ? OS_EINVALPARAMS : OS_EOK;
}

oserr_t
HCITransferElementsNeeded(
    _In_  UsbManagerTransfer_t*     transfer,
    _In_  uint32_t                  transferLength,
    _In_  enum USBTransferDirection direction,
        _Out_ int*                     elementCountOut)
{
    // Build elements from the device segments in the current DMA mapping.
    _CRT_UNUSED(direction);
    return __BuildTransferElements(transfer, transferLength, NULL, elementCountOut);
}

void
HCITransferElementFill(
    _In_ UsbManagerTransfer_t*     transfer,
    _In_ uint32_t                  transferLength,
    _In_ enum USBTransferDirection direction)
{
    int count;

    _CRT_UNUSED(direction);
    // The count pass must validate these segments before allocating element
    // storage. The segments remain unchanged when filling that storage.
    (void)__BuildTransferElements(transfer, transferLength, transfer->Elements, &count);
}

oserr_t
XhciTransferPrepare(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer,
    _In_ XhciEndpoint_t*       endpoint)
{
    XhciTransferDescriptor_t* descriptor;
    XhciTransferDescriptor_t* tail = NULL;
    int                      descriptorCount;
    int                      i;
    oserr_t                  oserr;

    if (transfer->RootElement != NULL) {
        return OS_EOK;
    }

    descriptorCount = __TransferElementsRemaining(transfer);
    transfer->ChainLength = descriptorCount;
    for (i = 0; i < descriptorCount; i++) {
        descriptor = transfer->ReusableElements;
        if (descriptor != NULL) {
            transfer->ReusableElements = descriptor->CacheNext;
        } else {
            oserr = UsbSchedulerAllocateElement(
                controller->Base.Scheduler,
                XHCI_TD_POOL,
                (uint8_t**)&descriptor
            );
            if (oserr != OS_EOK) {
                XhciTransferCleanup(controller, transfer);
                return oserr;
            }
        }

        __BuildDescriptor(descriptor, transfer, endpoint, endpoint->TransferRing.EnqueueIndex);
        if (transfer->RootElement == NULL) {
            transfer->RootElement = descriptor;
            tail = descriptor;
            continue;
        }

        // Append at the tail to avoid walking the chain for each element.
        // This keeps fragmented transfer construction linear in element count.
        oserr = UsbSchedulerChainElement(
                controller->Base.Scheduler,
                XHCI_TD_POOL,
                (uint8_t*)tail,
                XHCI_TD_POOL,
                (uint8_t*)descriptor,
                USB_ELEMENT_NO_INDEX,
                USB_CHAIN_DEPTH
        );
        if (oserr != OS_EOK) {
            UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)descriptor);
            XhciTransferCleanup(controller, transfer);
            return oserr;
        }
        tail = descriptor;
    }
    return OS_EOK;
}

bool
XhciTransferIsSetAddress(
    _In_  UsbManagerTransfer_t* transfer,
    _Out_ uint8_t*              addressOut)
{
    const usb_packet_t* packet = &transfer->Dma.Setup;

    if (transfer->Type != USBTRANSFER_TYPE_CONTROL || transfer->ElementCount < 2) {
        return false;
    }
    if (packet->Type != USBPACKET_TYPE_SET_ADDRESS ||
        packet->Direction != USBPACKET_DIRECTION_OUT) {
        return false;
    }

    *addressOut = packet->ValueLo;
    return true;
}

void
XhciTransferCompleteSoftware(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciTransferDescriptor_t* descriptor = transfer->RootElement;

    while (descriptor != NULL) {
        descriptor->BytesTransferred = descriptor->TransferLength;
        descriptor->CompletionCode = XHCI_TRB_COMPLETION_SUCCESS;
        descriptor->Flags |= XHCI_TD_FLAG_COMPLETED;
        descriptor = __DescriptorNext(controller, descriptor);
    }
    UsbManagerQueueTransferWork(&controller->Base, transfer);
}

oserr_t
XhciTransferSubmit(
    _In_ XhciController_t*     controller,
    _In_ XhciEndpoint_t*       endpoint,
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciTransferDescriptor_t* descriptor = transfer->RootElement;
    int                       elementIndex = __Transfer_IsAsync(transfer) ?
            transfer->TData.Async.ElementsCompleted : 0;

    XhciTrb_t trb;
    uint16_t trbIndex;
    oserr_t status;

    if (controller->State != XHCI_CONTROLLER_RUNNING) {
        return OS_EBUSY;
    }
    if (endpoint->TransferRing.Used + transfer->ChainLength >
            (uint16_t)(endpoint->TransferRing.TrbCount - 1)) {
        return OS_EBUSY;
    }

    // The payload must be ready for DMA before a TRB cycle bit allows the
    // controller to access it.
    if (!transfer->Dma.DeviceOwned) {
        status = DMABeginTransfer(transfer->Dma.Mapping, 0, transfer->Dma.Length);
        if (status != OS_EOK) {
            return status;
        }
        transfer->Dma.DeviceOwned = true;
    }

    transfer->Dma.HardwareActive = true;
    while (descriptor != NULL) {
        __BuildTrb(transfer, elementIndex, &trb);
        status = XhciRingEnqueue(&endpoint->TransferRing, &trb, &trbIndex);
        if (status != OS_EOK) {
            return status;
        }

        descriptor->FirstTrbIndex = trbIndex;
        descriptor->LastTrbIndex = trbIndex;
        descriptor->TrbCount = 1;
        descriptor->TransferLength = transfer->Elements[elementIndex].Length;
        descriptor->CycleState = endpoint->TransferRing.Trbs[trbIndex].Control & XHCI_TRB_CONTROL_CYCLE ? 1 : 0;
        // Reset completion state when a stopped or stalled TD is resubmitted.
        descriptor->CompletionCode = 0;
        descriptor->BytesTransferred = 0;
        descriptor->Flags = XHCI_TD_FLAG_QUEUED;
        endpoint->TRBOwners[trbIndex] = descriptor;

        descriptor = __DescriptorNext(controller, descriptor);
        elementIndex++;
    }

    endpoint->CurrentTd = transfer->RootElement;
    dma_mb();
    WRITE_VOLATILE(controller->Doorbells[endpoint->SlotId], endpoint->DeviceContextIndex);
    return OS_EOK;
}

bool
XhciTransferHandleEvent(
    _In_ XhciController_t* controller,
    _In_ XhciTrb_t*        eventTrb)
{
    XhciDevice_t*            device;
    XhciEndpoint_t*          endpoint;
    XhciTransferDescriptor_t* eventDescriptor;
    XhciTransferDescriptor_t* descriptor;
    XhciTransferDescriptor_t* next;
    UsbManagerTransfer_t*    transfer;
    uint64_t                address = eventTrb->Parameter;
    uint64_t                offset;
    uint32_t                code = XHCI_TRB_COMPLETION_CODE(eventTrb->Status);
    uint32_t                remaining = XHCI_TRB_TRANSFER_LENGTH(eventTrb->Status);
    uint16_t                index;
    uint16_t                released;
    uint8_t                 slotId = XHCI_TRB_SLOT_ID(eventTrb->Control);
    uint8_t                 endpointId = XHCI_TRB_ENDPOINT_ID(eventTrb->Control);
    bool                    failed = code != XHCI_TRB_COMPLETION_SUCCESS && code != XHCI_TRB_COMPLETION_SHORT_PACKET;

    if (slotId == 0 || slotId > controller->SlotCount || endpointId == 0) {
        return false;
    }
    device = controller->Slots[slotId];
    if (device == NULL) {
        return false;
    }
    endpoint = device->Endpoints[endpointId];
    if (endpoint == NULL || address < endpoint->TransferRing.DeviceBase) {
        return false;
    }

    // The slot and endpoint select the ring. Before changing transfer state,
    // require an aligned TRB address in that ring with a transfer descriptor.
    offset = address - endpoint->TransferRing.DeviceBase;
    if (offset >= endpoint->TransferRing.TrbCount * sizeof(XhciTrb_t) || offset % sizeof(XhciTrb_t)) {
        return false;
    }
    index = (uint16_t)(offset / sizeof(XhciTrb_t));
    eventDescriptor = endpoint->TRBOwners[index];
    if (eventDescriptor == NULL) {
        return false;
    }
    transfer = eventDescriptor->Transfer;
    descriptor = transfer->RootElement;
    while (descriptor != NULL) {
        if (!(descriptor->Flags & XHCI_TD_FLAG_COMPLETED)) {
            descriptor->CompletionCode = descriptor == eventDescriptor ? code : XHCI_TRB_COMPLETION_SUCCESS;
            descriptor->BytesTransferred = descriptor->TransferLength;
            if (descriptor == eventDescriptor) {
                descriptor->BytesTransferred -= MIN(descriptor->TransferLength, remaining);
            }
            descriptor->Flags |= XHCI_TD_FLAG_COMPLETED;
        }
        if (descriptor == eventDescriptor) {
            break;
        }
        descriptor = __DescriptorNext(controller, descriptor);
    }
    next = __DescriptorNext(controller, eventDescriptor);
    if (failed) {
        eventDescriptor->Flags |= XHCI_TD_FLAG_FAILED;
        endpoint->State = XHCI_ENDPOINT_HALTED;
    }
    if (code == XHCI_TRB_COMPLETION_SHORT_PACKET) {
        // Hardware skips the remaining data TRBs after a short packet.
        // Exclude skipped data from the byte count. Control transfers must
        // still complete the status stage.
        descriptor = next;
        while (descriptor != NULL) {
            if (transfer->Type == USBTRANSFER_TYPE_CONTROL && __DescriptorNext(controller, descriptor) == NULL) {
                break;
            }
            descriptor->BytesTransferred = 0;
            descriptor->CompletionCode = XHCI_TRB_COMPLETION_SUCCESS;
            descriptor->Flags |= XHCI_TD_FLAG_COMPLETED;
            descriptor = __DescriptorNext(controller, descriptor);
        }
        if (transfer->Type == USBTRANSFER_TYPE_CONTROL && next != NULL) {
            return false;
        }
    } else if (!failed && next != NULL) {
        return false;
    }

    // Clear descriptor references for every TRB, including skipped entries,
    // before the transfer manager can free the descriptor chain and mapping.
    released = 0;
    descriptor = transfer->RootElement;
    while (descriptor != NULL) {
        index = descriptor->FirstTrbIndex;
        if (endpoint->TRBOwners[index] == descriptor) {
            endpoint->TRBOwners[index] = NULL;
            released += descriptor->TrbCount;
        }
        descriptor = __DescriptorNext(controller, descriptor);
    }
    XhciRingRelease(&endpoint->TransferRing, released);
    transfer->Dma.HardwareActive = false;
    UsbManagerQueueTransferWork(&controller->Base, transfer);
    return true;
}

void
XhciTransferMarkCancelled(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciTransferDescriptor_t* descriptor = transfer->RootElement;

    while (descriptor != NULL) {
        descriptor->Flags |= XHCI_TD_FLAG_CANCELLED;
        descriptor = __DescriptorNext(controller, descriptor);
    }
}

void
XhciTransferCleanup(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciTransferDescriptor_t* descriptor;

    if (transfer == NULL) {
        return;
    }

    descriptor = transfer->RootElement;
    if (descriptor != NULL) {
        if (descriptor->Endpoint != NULL) {
            XhciEndpointDequeueTransfer(descriptor->Endpoint, descriptor);
        }
        UsbManagerChainEnumerate(
            &controller->Base,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_CLEANUP,
            HCIProcessElement,
            transfer
        );
        transfer->RootElement = NULL;
    }

    while (transfer->ReusableElements != NULL) {
        descriptor = transfer->ReusableElements;
        transfer->ReusableElements = descriptor->CacheNext;
        UsbSchedulerFreeElement(controller->Base.Scheduler, (uint8_t*)descriptor);
    }
}

static void
__RecycleDescriptors(
    _In_    XhciController_t*     controller,
    _InOut_ UsbManagerTransfer_t* transfer)
{
    XhciTransferDescriptor_t* descriptor = transfer->RootElement;
    XhciTransferDescriptor_t* next;

    if (descriptor != NULL && descriptor->Endpoint != NULL) {
        XhciEndpointDequeueTransfer(descriptor->Endpoint, descriptor);
    }
    while (descriptor != NULL) {
        next = __DescriptorNext(controller, descriptor);
        // Clear the endpoint pointer before caching the descriptor, since
        // detach may free the endpoint.
        descriptor->Endpoint = NULL;
        descriptor->CacheNext = transfer->ReusableElements;
        transfer->ReusableElements = descriptor;
        descriptor = next;
    }
    transfer->RootElement = NULL;
}

oserr_t
HCITransferFinalize(
    _In_ UsbManagerController_t* controllerBase,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ bool                    deferredClean)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;
    bool              keepDescriptors;
    oserr_t           status;

    status = USBTransferDmaComplete(transfer, 0);
    if (status != OS_EOK) {
        return status;
    }
    keepDescriptors = __Transfer_IsPeriodic(transfer) && transfer->State == USBTRANSFER_STATE_QUEUED;
    if (transfer->ResultCode == USBTRANSFERCODE_CANCELLED || deferredClean) {
        keepDescriptors = false;
    }
    if (keepDescriptors) {
        if (transfer->ResultCode == USBTRANSFERCODE_STALL) {
            // Keep the completed chain in the endpoint's pending list until
            // reset recovery resubmits it, or cancellation or detach removes it.
            return OS_EOK;
        }
        // Cache completed descriptors for the next periodic transfer.
        // Cancellation and controller teardown free active and cached descriptors.
        __RecycleDescriptors(controller, transfer);
    } else {
        XhciTransferCleanup(controller, transfer);
    }
    return OS_EOK;
}

oserr_t
HCITransferQueue(
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciController_t*         controller;
    XhciEndpoint_t*           endpoint;
    XhciTransferDescriptor_t* rootDescriptor;
    oserr_t                   oserr;

    controller = (XhciController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller == NULL) {
        return OS_ENOENT;
    }
    if (controller->State != XHCI_CONTROLLER_RUNNING) {
        return OS_EBUSY;
    }

    endpoint = XhciEndpointGetOrCreate(controller, transfer);
    if (endpoint == NULL) {
        return OS_EOOM;
    }

    {
        XhciDevice_t* device;
        uint8_t       address;

        oserr = XhciDeviceEnsure(controller, transfer, endpoint, &device);
        if (oserr == OS_EINCOMPLETE) {
            UsbManagerWaitTransfer(&controller->Base, transfer);
            return OS_EOK;
        }
        if (oserr != OS_EOK) {
            return oserr;
        }

        // Before submitting new TRBs, recovery must finish DMA on every old
        // mapping so the CPU can access it. Recovery completion wakes waiters.
        switch (endpoint->State) {
            case XHCI_ENDPOINT_RESET_PENDING:
            case XHCI_ENDPOINT_STOP_PENDING:
            case XHCI_ENDPOINT_DEQUEUE_PENDING:
            case XHCI_ENDPOINT_REBUILD_PENDING:
                UsbManagerWaitTransfer(&controller->Base, transfer);
                return OS_EOK;
            case XHCI_ENDPOINT_RETIRE_PENDING:
            case XHCI_ENDPOINT_FAILED:
                return OS_EUNKNOWN;
            default:
                break;
        }

        // For a non-control endpoint, the first transfer or changed metadata
        // requires Configure Endpoint. Wait for the command to complete before
        // building and submitting the transfer's TRBs.
        if (endpoint->DeviceContextIndex != 1) {
            if (device->State != XHCI_DEVICE_ADDRESSED) {
                UsbManagerWaitTransfer(&controller->Base, transfer);
                return OS_EOK;
            }
            if (endpoint->State == XHCI_ENDPOINT_FAILED) {
                return OS_EUNKNOWN;
            }
            if (endpoint->State == XHCI_ENDPOINT_CONFIGURE_PENDING) {
                UsbManagerWaitTransfer(&controller->Base, transfer);
                return OS_EOK;
            }
            if (!XhciEndpointMetadataMatches(endpoint, transfer)) {
                oserr = XhciDeviceConfigureEndpoint(controller, device, endpoint, transfer);
                if (oserr == OS_EINCOMPLETE) {
                    UsbManagerWaitTransfer(&controller->Base, transfer);
                    return OS_EOK;
                }
                if (oserr != OS_EOK) {
                    return oserr;
                }
            }
        }

        oserr = XhciTransferPrepare(controller, transfer, endpoint);
        if (oserr != OS_EOK) {
            return oserr;
        }

        if (XhciTransferIsSetAddress(transfer, &address)) {
            oserr = XhciDeviceSetAddress(controller, device, transfer, address);
            if (oserr == OS_EINCOMPLETE) {
                transfer->State = USBTRANSFER_STATE_QUEUED;
                return OS_EOK;
            }
            XhciTransferCleanup(controller, transfer);
            return oserr;
        }
    }

    rootDescriptor = (XhciTransferDescriptor_t*)transfer->RootElement;
    oserr = XhciEndpointEnqueueTransfer(endpoint, rootDescriptor);
    if (oserr != OS_EOK) {
        XhciTransferCleanup(controller, transfer);
        return oserr;
    }

    oserr = XhciTransferSubmit(controller, endpoint, transfer);
    if (oserr != OS_EOK) {
        XhciTransferCleanup(controller, transfer);
        return oserr;
    }

    transfer->State = USBTRANSFER_STATE_QUEUED;
    return OS_EOK;
}

oserr_t
HCITransferQueueIsochronous(
    _In_ UsbManagerTransfer_t* transfer)
{
    return HCITransferQueue(transfer);
}

oserr_t
HCITransferDequeue(
    _In_ UsbManagerTransfer_t* transfer)
{
    XhciController_t* controller = (XhciController_t*)UsbManagerGetController(transfer->DeviceID);
    XhciEndpoint_t*   endpoint;

    if (controller == NULL) {
        return OS_ENOENT;
    }

    if (!transfer->Dma.HardwareActive &&
        (transfer->RootElement == NULL || transfer->State == USBTRANSFER_STATE_STALLED)) {
        transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        UsbManagerQueueTransferWork(&controller->Base, transfer);
        UsbManagerProcessTransfers(&controller->Base);
        return OS_EOK;
    }

    endpoint = XhciEndpointGet(controller, &transfer->Address);
    if (endpoint == NULL || transfer->RootElement == NULL) {
        return OS_ENOENT;
    }

    // Stop Endpoint and Set TR Dequeue Pointer must complete before freeing
    // the cancelled TD's storage. Then rebuild and requeue the other transfers
    // on the endpoint.
    return XhciEndpointCancelTransfer(controller, endpoint, transfer);
}
