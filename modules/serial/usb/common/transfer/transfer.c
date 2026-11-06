/**
 * Copyright 2023, Philip Meulengracht
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

//#define __TRACE
#define __need_static_assert

#include <assert.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/shm.h>
#include "../hci.h"
#include "transfer.h"

#include "ctt_driver_service_server.h"
#include "ctt_usbhost_service_server.h"

extern gracht_server_t* __crt_get_module_server(void);

static void
__DiscardTransfer(
    _In_ UsbManagerTransfer_t* transfer)
{
    UsbManagerController_t* controller;

    transfer->Submitting = true;
    if (USBTransferDestroy(transfer) == OS_EOK) {
        return;
    }

    // The caller sends the response when creation or queueing fails. Keep the
    // transfer in the controller list so cleanup can retry without sending another.
    transfer->Submitting = false;
    transfer->Flags |= __USBTRANSFER_FLAG_SILENT;
    transfer->ResultCode = USBTRANSFERCODE_INVALID;
    transfer->State = USBTRANSFER_STATE_CLEANUP;
    controller = UsbManagerGetController(transfer->DeviceID);
    UsbManagerQueueTransferWork(controller, transfer);
}

static unsigned int
__ConvertFlags(
        _In_ unsigned int flags)
{
    unsigned int tflags = 0;
    if (flags & USB_TRANSFER_NO_NOTIFICATION) {
        tflags |= __USBTRANSFER_FLAG_SILENT;
    }
    if (flags & USB_TRANSFER_SHORT_NOT_OK) {
        tflags |= __USBTRANSFER_FLAG_FAILONSHORT;
    }
    return tflags;
}

oserr_t
USBTransferCreate(
        _In_  struct gracht_message* message,
        _In_  USBTransfer_t*         transfer,
        _In_  uuid_t                 transferId,
        _In_  uuid_t                 deviceId,
        _Out_ UsbManagerTransfer_t** transferOut)
{
    UsbManagerController_t* controller;
    UsbManagerTransfer_t*   usbTransfer;
    oserr_t                 oserr;
    
    TRACE("USBTransferCreate(transfer=0x%" PRIxIN ", message=0x%" PRIxIN ", deviceId=%u)",
        transfer, message, deviceId);

    controller = (UsbManagerController_t*)UsbManagerGetController(deviceId);
    if (!controller || controller->State.Phase >= USB_CONTROLLER_DESTROY_PENDING) {
        return OS_ENOENT;
    }

    usbTransfer = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!usbTransfer) {
        return OS_EOOM;
    }
    
    memset(usbTransfer, 0, sizeof(UsbManagerTransfer_t));
    gracht_server_defer_message(message, &usbTransfer->DeferredMessage[0]);

    ELEMENT_INIT(&usbTransfer->ListHeader, 0, usbTransfer);
    usbTransfer->Submitting = true;
    usbTransfer->ID = transferId;
    usbTransfer->DeviceID = deviceId;
    usbTransfer->Type = transfer->Type;
    usbTransfer->Speed = transfer->Speed;
    usbTransfer->Direction = transfer->Direction;
    memcpy(&usbTransfer->Address, &transfer->Address, sizeof(USBAddress_t));
    usbTransfer->MaxPacketSize = transfer->MaxPacketSize;
    usbTransfer->Flags = __ConvertFlags(transfer->Flags);

    // Add the transfer before allocating resources. If cleanup fails, controller
    // teardown and the cleanup worker must still be able to find it.
    list_append(&controller->TransactionList, &usbTransfer->ListHeader);

    if (__Transfer_IsPeriodic(usbTransfer)) {
        // The endpoint value includes the first transaction. The descriptor counts
        // only additional transactions. Use one transaction when the value is zero.
        usbTransfer->TData.Periodic.Bandwith = transfer->PeriodicBandwith != 0 ?
            transfer->PeriodicBandwith - 1 : 0;
        usbTransfer->TData.Periodic.Interval = transfer->PeriodicInterval;
    }

    if (transfer->MaxPacketSize == 0 || transfer->Length == 0) {
        __DiscardTransfer(usbTransfer);
        return OS_EINVALPARAMS;
    }

    usbTransfer->Dma.Context = controller->DmaContext;
    usbTransfer->Dma.PacketPool = controller->BouncePool;
    usbTransfer->Dma.BufferOffset = transfer->BufferOffset;
    usbTransfer->Dma.BufferLength = transfer->Length;
    if (__Transfer_IsPeriodic(usbTransfer)) {
        usbTransfer->TData.Periodic.BufferSize = transfer->Length;
    }

    oserr = SHMAttach(transfer->BufferHandle, &usbTransfer->SHMHandle);
    if (oserr != OS_EOK) {
        WARNING("USB DMA: transfer %u attach failed (%u)", transferId, oserr);
        __DiscardTransfer(usbTransfer);
        return oserr;
    }
    
    oserr = USBTransferDmaPrepare(usbTransfer);
    if (oserr != OS_EOK) {
        WARNING("USB DMA: transfer %u dma preparation failed (%u)", transferId, oserr);
        __DiscardTransfer(usbTransfer);
        return oserr;
    }

    usbTransfer->Submitting = false;
    *transferOut = usbTransfer;
    return OS_EOK;

}

oserr_t
USBTransferDestroy(
    _In_ UsbManagerTransfer_t* transfer)
{
    UsbManagerController_t* controller;
    oserr_t                 status;

    if (!transfer) {
        return OS_EOK;
    }

    if (transfer->RootElement != NULL || transfer->Dma.Linked) {
        return OS_EBUSY;
    }

    // Unmapping must succeed before freeing storage, even if the controller failed to stop.
    status = USBTransferDmaDestroy(transfer);
    if (status != OS_EOK) {
        return status;
    }
    
    // Keep the transfer in the controller list until its client attachment is
    // released. The manager needs the SHM handle if cleanup must retry.
    status = SHMRelease(&transfer->SHMHandle);
    if (status != OS_EOK) {
        WARNING("USB transfer SHM release failed (%u); keeping transfer", status);
        return status;
    }

    controller = (UsbManagerController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller) {
        list_remove(&controller->TransactionList, &transfer->ListHeader);
    }
    UsbManagerForgetTransferWork(transfer);
    free(transfer->Elements);
    free(transfer);
    return OS_EOK;
}

void
USBTransferNotify(
    _In_ UsbManagerTransfer_t* transfer)
{
    uint32_t offset = transfer->TData.Periodic.CurrentDataIndex;

    if (transfer->Flags & (__USBTRANSFER_FLAG_SILENT | __USBTRANSFER_FLAG_NOTIFIED)) {
        return;
    }

    // IPC can yield to cancellation, so mark the transfer notified before sending.
    // Advance the client slot while the length still describes this packet.
    transfer->Flags |= __USBTRANSFER_FLAG_NOTIFIED;
    if (__Transfer_IsAsync(transfer)) {
        ctt_usbhost_queue_response(&transfer->DeferredMessage[0], OS_EOK,
            from_usbcode(transfer->ResultCode), transfer->TData.Async.BytesTransferred);
    } else {
        if (transfer->ResultCode == USBTRANSFERCODE_SUCCESS) {
            transfer->TData.Periodic.CurrentDataIndex = ADDLIMIT(0, offset,
                transfer->Dma.Length, transfer->TData.Periodic.BufferSize);
        }

        if (transfer->Type == USBTRANSFER_TYPE_INTERRUPT) {
            ctt_usbhost_event_transfer_status_single(__crt_get_module_server(),
                transfer->DeferredMessage[0].client, transfer->ID,
                from_usbcode(transfer->ResultCode), offset);
        }
    }
}

void ctt_usbhost_queue_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t deviceId,
                                  const uuid_t transferId, const uint8_t* transfer, const uint32_t transfer_count)
{
    UsbManagerTransfer_t*   usbTransfer;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    oserr_t                 oserr;

    if (controller == NULL || controller->State.Phase >= USB_CONTROLLER_DESTROY_PENDING) {
        ctt_usbhost_queue_response(message, OS_ENOENT, CTT_USB_TRANSFER_STATUS_NAK, 0);
        return;
    }
    
    controller->State.OperationsInFlight++;

    oserr = USBTransferCreate(
            message,
            (USBTransfer_t*)transfer,
            transferId,
            deviceId,
            &usbTransfer
    );
    if (oserr != OS_EOK) {
        controller->State.OperationsInFlight--;
        ctt_usbhost_queue_response(message, oserr, CTT_USB_TRANSFER_STATUS_NAK, 0);
        return;
    }

    usbTransfer->Submitting = true;
    oserr = HCITransferQueue(usbTransfer);
    if (oserr != OS_EOK) {
        WARNING("USB: transfer %u submission failed (%u)", transferId, oserr);
        __DiscardTransfer(usbTransfer);
        ctt_usbhost_queue_response(message, oserr, CTT_USB_TRANSFER_STATUS_NAK, 0);
    } else {
        usbTransfer->Submitting = false;
    }

    controller->State.OperationsInFlight--;
}

void ctt_usbhost_queue_periodic_invocation(struct gracht_message* message, const uuid_t processId,
                                           const uuid_t deviceId, const uuid_t transferId, const uint8_t* transfer, const uint32_t transfer_count)
{
    UsbManagerTransfer_t*   usbTransfer;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    oserr_t                 oserr;

    if (controller == NULL || controller->State.Phase >= USB_CONTROLLER_DESTROY_PENDING) {
        ctt_usbhost_queue_periodic_response(message, OS_ENOENT);
        return;
    }

    controller->State.OperationsInFlight++;
    oserr = USBTransferCreate(
            message,
            (USBTransfer_t*)transfer,
            transferId,
            deviceId,
            &usbTransfer
    );
    if (oserr != OS_EOK) {
        controller->State.OperationsInFlight--;
        ctt_usbhost_queue_periodic_response(message, oserr);
        return;
    }

    usbTransfer->Submitting = true;
    if (usbTransfer->Type == USBTRANSFER_TYPE_ISOC) {
        oserr = HCITransferQueueIsochronous(usbTransfer);
    } else {
        oserr = HCITransferQueue(usbTransfer);
    }

    if (oserr != OS_EOK) {
        __DiscardTransfer(usbTransfer);
    } else {
        usbTransfer->Submitting = false;
    }
    controller->State.OperationsInFlight--;
    ctt_usbhost_queue_periodic_response(message, oserr);
}

void ctt_usbhost_reset_periodic_invocation(struct gracht_message* message, const uuid_t processId,
                                           const uuid_t deviceId, const uuid_t transferId)
{
    oserr_t                 status     = OS_ENOENT;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    UsbManagerTransfer_t*   transfer   = NULL;

    // Find the transfer for this client and transfer ID.
    if (controller != NULL) {
        foreach(node, &controller->TransactionList) {
            UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->value;
            if (itr->DeferredMessage[0].client == message->client &&
                itr->ID == transferId) {
                transfer = itr;
                break;
            }
        }
    }

    if (transfer != NULL && controller->State.Phase < USB_CONTROLLER_DESTROY_PENDING) {
        if (transfer->Submitting || (transfer->State != USBTRANSFER_STATE_COMPLETED &&
            !(controller->Kind != USBCONTROLLER_KIND_XHCI && transfer->State == USBTRANSFER_STATE_STALLED))) {
            status = OS_EBUSY;
        } else {
            controller->State.OperationsInFlight++;
            transfer->Submitting = true;
            
            status = USBTransferDmaPrepare(transfer);
            if (transfer->State == USBTRANSFER_STATE_CLEANUP ||
                controller->State.Phase >= USB_CONTROLLER_DESTROY_PENDING) {
                status = OS_EBUSY;
            }
            
            if (status == OS_EOK) {
                status = transfer->Type == USBTRANSFER_TYPE_ISOC ?
                    HCITransferQueueIsochronous(transfer) : HCITransferQueue(transfer);
            }
            transfer->Submitting = false;
            controller->State.OperationsInFlight--;
        }
    }

    ctt_usbhost_reset_periodic_response(message, status);
}

void ctt_usbhost_dequeue_invocation(struct gracht_message* message, const uuid_t processId,
                                    const uuid_t deviceId, const uuid_t transferId)
{
    oserr_t                 status     = OS_ENOENT;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    UsbManagerTransfer_t*   transfer   = NULL;

    // Find the transfer for this client and transfer ID.
    if (controller) {
        foreach(node, &controller->TransactionList) {
            UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->value;
            if (itr->DeferredMessage[0].client == message->client &&
                itr->ID == transferId) {
                transfer = itr;
                break;
            }
        }
    }

    // If no transfer matched, report OS_ENOENT.
    if (transfer != NULL) {
        status = HCITransferDequeue(transfer);
    }
    
    ctt_usbhost_dequeue_response(message, status);
}
