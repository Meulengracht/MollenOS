/**
 * Copyright 2026, Philip Meulengracht
 */

#define __need_minmax
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

void
XhciTransferCleanup(
    _In_ XhciController_t*     controller,
    _In_ UsbManagerTransfer_t* transfer)
{
    if (transfer == NULL || transfer->RootElement == NULL) {
        return;
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

    oserr = XhciTransferPrepare(controller, transfer, endpoint);
    if (oserr != OS_EOK) {
        return oserr;
    }

    rootDescriptor = (XhciTransferDescriptor_t*)transfer->RootElement;
    oserr = XhciEndpointEnqueueTransfer(endpoint, rootDescriptor);
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

    if (controller == NULL) {
        return OS_ENOENT;
    }

    XhciTransferCleanup(controller, transfer);
    return OS_EOK;
}
