/**
 * Copyright 2026, Philip Meulengracht
 */

#define __need_minmax
#include <ddk/barrier.h>
#include <ddk/io.h>
#include <os/shm.h>
#include <string.h>
#include "xhci.h"

static inline uintptr_t
__GetAddress(
    _In_ SHMSGTable_t* sgTable,
    _In_ int           index,
    _In_ size_t        offset)
{
    return sgTable->Entries[index].Address + offset;
}

static inline size_t
__GetBytesLeft(
    _In_ SHMSGTable_t* sgTable,
    _In_ int           index,
    _In_ size_t        offset)
{
    return sgTable->Entries[index].Length - offset;
}

static void
__FillElement(
    _In_  UsbManagerTransfer_t* transfer,
    _In_  SHMSGTable_t*         sgTable,
    _In_  uint32_t              sgTableOffset,
    _In_  uint32_t              transferLength,
    _In_  uint32_t              bytesLeft,
    _Out_ struct TransferElement* element)
{
    int    index;
    size_t offset;

    SHMSGTableOffset(
            sgTable,
            sgTableOffset + (transferLength - bytesLeft),
            &index,
            &offset
    );

    element->Data.Address = __GetAddress(sgTable, index, offset);
    element->Length       = (uint32_t)MIN(bytesLeft, MIN(__GetBytesLeft(sgTable, index, offset), XHCI_TRB_MAX_DATA));
    element->Type         = __TransferElement_DirectionToType(transfer->Direction);
}

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

static bool
__ReadSetupPacket(
    _In_  UsbManagerTransfer_t* transfer,
    _Out_ usb_packet_t*         packetOut)
{
    SHMSGTable_t sgTable;
    uint8_t*     buffer;
    size_t       virtualOffset = 0;
    uintptr_t    setupAddress = transfer->Elements[0].Data.Address;

    if (SHMGetSGTable(&transfer->SHMHandle, &sgTable, -1) != OS_EOK) {
        return false;
    }

    buffer = SHMBuffer(&transfer->SHMHandle);
    for (int i = 0; i < sgTable.Count; i++) {
        uintptr_t entryStart = sgTable.Entries[i].Address;
        uintptr_t entryEnd = entryStart + sgTable.Entries[i].Length;

        if (setupAddress >= entryStart &&
            setupAddress + sizeof(usb_packet_t) <= entryEnd) {
            memcpy(packetOut, buffer + virtualOffset + (setupAddress - entryStart), sizeof(usb_packet_t));
            free(sgTable.Entries);
            return true;
        }
        virtualOffset += sgTable.Entries[i].Length;
    }

    free(sgTable.Entries);
    return false;
}

static oserr_t
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
            usb_packet_t packet;

            if (!__ReadSetupPacket(transfer, &packet)) {
                return OS_EUNKNOWN;
            }
            memcpy(&trbOut->Parameter, &packet, sizeof(usb_packet_t));
            trbOut->Status = sizeof(usb_packet_t);
            trbOut->Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_SETUP_STAGE) |
                    XHCI_TRB_CONTROL_IDT | XHCI_TRB_CONTROL_CHAIN;
            if (transfer->ElementCount > 2) {
                trbOut->Control |= transfer->Direction == USBTRANSFER_DIRECTION_IN ?
                        XHCI_TRB_CONTROL_TRT_IN : XHCI_TRB_CONTROL_TRT_OUT;
            } else {
                trbOut->Control |= XHCI_TRB_CONTROL_TRT_NONE;
            }
            return OS_EOK;
        }

        if (isLast) {
            trbOut->Parameter = 0;
            trbOut->Status = 0;
            trbOut->Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_STATUS_STAGE) |
                    XHCI_TRB_CONTROL_IOC;
            if (element->Type == TRANSFERELEMENT_TYPE_IN) {
                trbOut->Control |= XHCI_TRB_CONTROL_DIR_IN;
            }
            return OS_EOK;
        }

        trbOut->Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_DATA_STAGE) |
                XHCI_TRB_CONTROL_CHAIN | XHCI_TRB_CONTROL_ISP;
        if (element->Type == TRANSFERELEMENT_TYPE_IN) {
            trbOut->Control |= XHCI_TRB_CONTROL_DIR_IN;
        }
        return OS_EOK;
    }

    trbOut->Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_NORMAL) |
            XHCI_TRB_CONTROL_ISP | (isLast ? XHCI_TRB_CONTROL_IOC : XHCI_TRB_CONTROL_CHAIN);
    return OS_EOK;
}

oserr_t
HCITransferElementsNeeded(
    _In_  UsbManagerTransfer_t*     transfer,
    _In_  uint32_t                  transferLength,
    _In_  enum USBTransferDirection direction,
    _In_  SHMSGTable_t*             sgTable,
    _In_  uint32_t                  sgTableOffset,
    _Out_ int*                      elementCountOut)
{
    uint32_t bytesLeft = transferLength;
    int      tdCount   = 0;

    _CRT_UNUSED(direction);
    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        bytesLeft -= sizeof(usb_packet_t);
        tdCount += 2;
    }

    while (bytesLeft) {
        int    index;
        size_t offset;
        size_t bytesInChunk;

        SHMSGTableOffset(
                sgTable,
                sgTableOffset + (transferLength - bytesLeft),
                &index,
                &offset
        );

        bytesInChunk = MIN(bytesLeft, MIN(__GetBytesLeft(sgTable, index, offset), XHCI_TRB_MAX_DATA));
        bytesLeft   -= (uint32_t)bytesInChunk;
        tdCount++;

        if (transfer->Type != USBTRANSFER_TYPE_ISOC &&
            bytesLeft == 0 &&
            bytesInChunk == transfer->MaxPacketSize &&
            transfer->Direction == USBTRANSFER_DIRECTION_OUT)
        {
            tdCount++;
        }
    }

    if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
        tdCount++;
    }
    *elementCountOut = tdCount;
    return tdCount == 0 ? OS_EINVALPARAMS : OS_EOK;
}

void
HCITransferElementFill(
    _In_ UsbManagerTransfer_t*     transfer,
    _In_ uint32_t                  transferLength,
    _In_ enum USBTransferDirection direction,
    _In_ SHMSGTable_t*             sgTable,
    _In_ uint32_t                  sgTableOffset)
{
    uint32_t                 bytesLeft = transferLength;
    enum TransferElementType ackType   = TRANSFERELEMENT_TYPE_IN;
    int                      index     = 0;

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        __FillElement(transfer, sgTable, sgTableOffset, transferLength, bytesLeft, &transfer->Elements[index]);
        transfer->Elements[index].Type   = TRANSFERELEMENT_TYPE_SETUP;
        transfer->Elements[index].Length = sizeof(usb_packet_t);
        bytesLeft -= sizeof(usb_packet_t);
        index++;

        if (bytesLeft && direction == USBTRANSFER_DIRECTION_IN) {
            ackType = TRANSFERELEMENT_TYPE_OUT;
        }
    }

    while (bytesLeft) {
        __FillElement(transfer, sgTable, sgTableOffset, transferLength, bytesLeft, &transfer->Elements[index]);
        transfer->Elements[index].Type = __TransferElement_DirectionToType(direction);
        bytesLeft -= transfer->Elements[index].Length;
        index++;

        if (bytesLeft == 0) {
            if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
                transfer->Elements[index++].Type = __TransferElement_DirectionToType(direction);
            } else if (direction == USBTRANSFER_DIRECTION_OUT &&
                       transfer->Elements[index - 1].Length == transfer->MaxPacketSize) {
                transfer->Elements[index++].Type = TRANSFERELEMENT_TYPE_OUT;
            }
        }
    }

    if (transfer->Type == USBTRANSFER_TYPE_CONTROL) {
        transfer->Elements[index].Type = ackType;
    }
}

oserr_t
XhciTransferPrepare(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer,
    _In_ XhciEndpoint_t*       endpoint)
{
    int descriptorCount;

    if (transfer->RootElement != NULL) {
        return OS_EOK;
    }

    descriptorCount = __TransferElementsRemaining(transfer);
    transfer->ChainLength = descriptorCount;
    for (int i = 0; i < descriptorCount; i++) {
        XhciTransferDescriptor_t* descriptor;
        oserr_t                   oserr;

        oserr = UsbSchedulerAllocateElement(controller->Base.Scheduler, XHCI_TD_POOL, (uint8_t**)&descriptor);
        if (oserr != OS_EOK) {
            XhciTransferCleanup(controller, transfer);
            return oserr;
        }

        __BuildDescriptor(descriptor, transfer, endpoint, endpoint->TransferRing.EnqueueIndex);
        if (transfer->RootElement == NULL) {
            transfer->RootElement = descriptor;
            continue;
        }

        oserr = UsbSchedulerChainElement(
                controller->Base.Scheduler,
                XHCI_TD_POOL,
                transfer->RootElement,
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
    }
    return OS_EOK;
}

bool
XhciTransferIsSetAddress(
    _In_  UsbManagerTransfer_t* transfer,
    _Out_ uint8_t*              addressOut)
{
    usb_packet_t packet;

    if (transfer->Type != USBTRANSFER_TYPE_CONTROL ||
        transfer->ElementCount < 2 ||
        !__ReadSetupPacket(transfer, &packet) ||
        packet.Type != USBPACKET_TYPE_SET_ADDRESS ||
        packet.Direction != USBPACKET_DIRECTION_OUT) {
        return false;
    }

    *addressOut = packet.ValueLo;
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

    if (endpoint->TransferRing.Used + transfer->ChainLength >= endpoint->TransferRing.TrbCount) {
        return OS_EBUSY;
    }

    while (descriptor != NULL) {
        XhciTrb_t trb;
        uint16_t  trbIndex;
        oserr_t   oserr = __BuildTrb(transfer, elementIndex, &trb);

        if (oserr != OS_EOK) {
            return oserr;
        }
        oserr = XhciRingEnqueue(&endpoint->TransferRing, &trb, &trbIndex);
        if (oserr != OS_EOK) {
            return oserr;
        }

        descriptor->FirstTrbIndex = trbIndex;
        descriptor->LastTrbIndex = trbIndex;
        descriptor->TrbCount = 1;
        descriptor->TransferLength = transfer->Elements[elementIndex].Length;
        descriptor->CycleState = endpoint->TransferRing.Trbs[trbIndex].Control & XHCI_TRB_CONTROL_CYCLE ? 1 : 0;
        descriptor->Flags |= XHCI_TD_FLAG_QUEUED;
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
    uintptr_t trbAddress = (uintptr_t)eventTrb->Parameter;

    foreach(node, &controller->XhciEndpoints) {
        XhciEndpoint_t* endpoint = node->value;
        uintptr_t       ringEnd = endpoint->TransferRing.PhysicalBase +
                (endpoint->TransferRing.TrbCount * sizeof(XhciTrb_t));

        if (trbAddress >= endpoint->TransferRing.PhysicalBase && trbAddress < ringEnd) {
            uint16_t trbIndex = (uint16_t)((trbAddress - endpoint->TransferRing.PhysicalBase) /
                    sizeof(XhciTrb_t));
            XhciTransferDescriptor_t* eventDescriptor = endpoint->TRBOwners[trbIndex];
            XhciTransferDescriptor_t* descriptor;
            uint32_t completionCode = XHCI_TRB_COMPLETION_CODE(eventTrb->Status);
            uint32_t bytesRemaining = XHCI_TRB_TRANSFER_LENGTH(eventTrb->Status);
            uint16_t trbsReleased = 0;

            if (eventDescriptor == NULL) {
                return false;
            }

            descriptor = eventDescriptor->Transfer->RootElement;
            while (descriptor != NULL) {
                if (!(descriptor->Flags & XHCI_TD_FLAG_COMPLETED)) {
                    descriptor->CompletionCode = descriptor == eventDescriptor ?
                            completionCode : XHCI_TRB_COMPLETION_SUCCESS;
                    descriptor->BytesTransferred = descriptor == eventDescriptor ?
                            descriptor->TransferLength - MIN(descriptor->TransferLength, bytesRemaining) :
                            descriptor->TransferLength;
                    descriptor->Flags |= XHCI_TD_FLAG_COMPLETED;
                    if (descriptor == eventDescriptor &&
                        completionCode != XHCI_TRB_COMPLETION_SUCCESS &&
                        completionCode != XHCI_TRB_COMPLETION_SHORT_PACKET) {
                        descriptor->Flags |= XHCI_TD_FLAG_FAILED;
                        /* A transfer error (stall, babble, etc) halts the endpoint
                         * ring on real hardware; the class driver must clear
                         * ENDPOINT_HALT before further transfers can proceed. */
                        endpoint->State = XHCI_ENDPOINT_HALTED;
                    }

                    endpoint->TRBOwners[descriptor->FirstTrbIndex] = NULL;
                    trbsReleased += descriptor->TrbCount;
                }
                if (descriptor == eventDescriptor) {
                    break;
                }
                descriptor = __DescriptorNext(controller, descriptor);
            }
            XhciRingRelease(&endpoint->TransferRing, trbsReleased);
            if (eventDescriptor->Transfer->Type == USBTRANSFER_TYPE_CONTROL &&
                completionCode == XHCI_TRB_COMPLETION_SHORT_PACKET &&
                __DescriptorNext(controller, eventDescriptor) != NULL) {
                return false;
            }
            return true;
        }
    }
    return false;
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
    if (transfer == NULL || transfer->RootElement == NULL) {
        return;
    }

    {
        XhciTransferDescriptor_t* rootDescriptor = transfer->RootElement;
        if (rootDescriptor->Endpoint != NULL) {
            XhciEndpointDequeueTransfer(rootDescriptor->Endpoint, rootDescriptor);
        }
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

oserr_t
HCITransferFinalize(
    _In_ UsbManagerController_t* controllerBase,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ bool                    deferredClean)
{
    XhciController_t* controller = (XhciController_t*)controllerBase;

    _CRT_UNUSED(deferredClean);
    XhciTransferCleanup(controller, transfer);
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

    endpoint = XhciEndpointGetOrCreate(controller, transfer);
    if (endpoint == NULL) {
        return OS_EOOM;
    }

    {
        XhciDevice_t* device;
        uint8_t       address;

        oserr = XhciDeviceEnsure(controller, transfer, endpoint, &device);
        if (oserr == OS_EINCOMPLETE) {
            transfer->State = USBTRANSFER_STATE_WAITING;
            return OS_EOK;
        }
        if (oserr != OS_EOK) {
            return oserr;
        }

        /* Non-control endpoints are configured lazily: the first transfer for
         * a DCI (or one whose metadata no longer matches what's configured)
         * issues a Configure Endpoint command and waits for it to complete
         * before its TRBs are ever built/submitted. */
        if (endpoint->DeviceContextIndex != 1) {
            if (device->State != XHCI_DEVICE_ADDRESSED) {
                transfer->State = USBTRANSFER_STATE_WAITING;
                return OS_EOK;
            }
            if (endpoint->State == XHCI_ENDPOINT_FAILED) {
                return OS_EUNKNOWN;
            }
            if (endpoint->State == XHCI_ENDPOINT_CONFIGURE_PENDING) {
                transfer->State = USBTRANSFER_STATE_WAITING;
                return OS_EOK;
            }
            if (!XhciEndpointMetadataMatches(endpoint, transfer)) {
                oserr = XhciDeviceConfigureEndpoint(controller, device, endpoint, transfer);
                if (oserr == OS_EINCOMPLETE) {
                    transfer->State = USBTRANSFER_STATE_WAITING;
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

    if (transfer->State == USBTRANSFER_STATE_WAITING && transfer->RootElement == NULL) {
        transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        return OS_EOK;
    }

    endpoint = XhciEndpointGet(controller, &transfer->Address);
    if (endpoint == NULL || transfer->RootElement == NULL) {
        return OS_ENOENT;
    }

    /* Submitted rings require Stop Endpoint + Set TR Dequeue Pointer before
     * the cancelled TD's storage can be released safely; every other transfer
     * on the endpoint is rebuilt/requeued once that completes. */
    return XhciEndpointCancelTransfer(controller, endpoint, transfer);
}
