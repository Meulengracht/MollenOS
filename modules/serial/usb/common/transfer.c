/**
 * MollenOS
 *
 * Copyright 2018, Philip Meulengracht
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
 * USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */

//#define __TRACE
#define __need_static_assert

#include <assert.h>
#include <ddk/utils.h>
#include <os/shm.h>
#include "hci.h"
#include <stdlib.h>
#include "transfer.h"

#include "ctt_driver_service_server.h"
#include "ctt_usbhost_service_server.h"

extern gracht_server_t* __crt_get_module_server(void);

UsbManagerTransfer_t*
UsbManagerCreateTransfer(
        _In_ struct gracht_message* message,
        _In_ UsbTransfer_t*         transfer,
        _In_ uuid_t                 transferId,
        _In_ uuid_t                 deviceId)
{
    UsbManagerController_t* controller;
    UsbManagerTransfer_t*   usbTransfer;
    int                     i;
    
    TRACE("UsbManagerCreateTransfer(transfer=0x%" PRIxIN ", message=0x%" PRIxIN ", deviceId=%u)",
        transfer, message, deviceId);

    controller = (UsbManagerController_t*)UsbManagerGetController(deviceId);
    if (!controller) {
        return NULL;
    }

    usbTransfer = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!usbTransfer) {
        return NULL;
    }
    
    memset(usbTransfer, 0, sizeof(UsbManagerTransfer_t));

    memcpy(&usbTransfer->Transfer, transfer, sizeof(UsbTransfer_t));
    gracht_server_defer_message(message, &usbTransfer->DeferredMessage[0]);

    ELEMENT_INIT(&usbTransfer->ListHeader, 0, usbTransfer);
    usbTransfer->DeviceId = deviceId;
    usbTransfer->Id       = transferId;
    usbTransfer->Status   = TransferCreated;
    
    // When attaching to dma buffers make sure we don't attach
    // multiple times as we can then save a system call or two
    for (i = 0; i < usbTransfer->Transfer.TransactionCount; i++) {
        if (usbTransfer->Transfer.Transactions[i].BufferHandle == UUID_INVALID) {
            continue;
        }
        
        if (i != 0 && usbTransfer->Transfer.Transactions[i].BufferHandle ==
                usbTransfer->Transfer.Transactions[i - 1].BufferHandle) {
            memcpy(&usbTransfer->Transactions[i], &usbTransfer->Transactions[i - 1],
                sizeof(struct UsbManagerTransaction));
        } else if (i == 2 && usbTransfer->Transfer.Transactions[i].BufferHandle ==
                usbTransfer->Transfer.Transactions[i - 2].BufferHandle) {
            memcpy(&usbTransfer->Transactions[i], &usbTransfer->Transactions[i - 2],
                sizeof(struct UsbManagerTransaction));
        } else {
            SHMAttach(
                    usbTransfer->Transfer.Transactions[i].BufferHandle,
                    &usbTransfer->Transactions[i].SHM
            );

            SHMGetSGTable(
                    &usbTransfer->Transactions[i].SHM,
                    &usbTransfer->Transactions[i].SHMTable,
                    -1
            );
        }
        SHMSGTableOffset(
                &usbTransfer->Transactions[i].SHMTable,
                usbTransfer->Transfer.Transactions[i].BufferOffset,
                &usbTransfer->Transactions[i].SGIndex,
                &usbTransfer->Transactions[i].SGOffset
        );
    }

    list_append(&controller->TransactionList, &usbTransfer->ListHeader);
    return usbTransfer;
}

void
UsbManagerDestroyTransfer(
    _In_ UsbManagerTransfer_t* transfer)
{
    UsbManagerController_t* controller;
    int                     i;

    if (!transfer) {
        return;
    }

    controller = (UsbManagerController_t*)UsbManagerGetController(transfer->DeviceId);
    if (controller) {
        list_remove(&controller->TransactionList, &transfer->ListHeader);
    }

    for (i = 0; i < transfer->Transfer.TransactionCount; i++) {
        if (transfer->Transfer.Transactions[i].BufferHandle == UUID_INVALID) {
            continue;
        }
        
        if (i != 0 && transfer->Transfer.Transactions[i].BufferHandle ==
                      transfer->Transfer.Transactions[i - 1].BufferHandle) {
            // do nothing, we already freed
        } else if (i == 2 && transfer->Transfer.Transactions[i].BufferHandle ==
                           transfer->Transfer.Transactions[i - 2].BufferHandle) {
            // do nothing, we already freed
        } else {
            free(transfer->Transactions[i].SHMTable.Entries);
            SHMDetach(&transfer->Transactions[i].SHM);
        }
    }
    free(transfer);
}

void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t* transfer)
{
    size_t bytesTransferred;
    
    TRACE("UsbManagerSendNotification(transfer=0x%" PRIxIN ")", transfer);
    
    // If user doesn't want, ignore
    TRACE("UsbManagerSendNotification transfer type=%u", transfer->Transfer.Type);
    if (transfer->Transfer.Flags & USB_TRANSFER_NO_NOTIFICATION) {
        TRACE("UsbManagerSendNotification flags 0x%x requested no notification", transfer->Transfer.Flags);
        return;
    }

    // If notification has been sent on control/bulk do not send again
    if (transfer->Transfer.Type == USB_TRANSFER_CONTROL || transfer->Transfer.Type == USB_TRANSFER_BULK) {
        if ((transfer->Flags & TransferFlagNotified)) {
            return;
        }
        transfer->Flags |= TransferFlagNotified;
        bytesTransferred = transfer->Transactions[0].BytesTransferred;
        bytesTransferred += transfer->Transactions[1].BytesTransferred;
        bytesTransferred += transfer->Transactions[2].BytesTransferred;

        TRACE("UsbManagerSendNotification is notifiyng");
        ctt_usbhost_queue_response(&transfer->DeferredMessage[0], transfer->Status, bytesTransferred);
    } else if (transfer->Transfer.Type == USB_TRANSFER_INTERRUPT) {
        ctt_usbhost_event_transfer_status_single(__crt_get_module_server(), transfer->DeferredMessage[0].client,
                                              transfer->Id, transfer->Status, transfer->CurrentDataIndex);
        if (transfer->Status == TransferFinished) {
            transfer->CurrentDataIndex = ADDLIMIT(0, transfer->CurrentDataIndex,
                                                  transfer->Transfer.Transactions[0].Length,
                                                  transfer->Transfer.PeriodicBufferSize);
        }
    } else {
        WARNING("UsbManagerSendNotification ISOCHRONOUS WHAT TO DO");
    }
}

void ctt_usbhost_queue_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t deviceId,
                                  const uuid_t transferId, const uint8_t* transfer, const uint32_t transfer_count)
{
    UsbManagerTransfer_t* usbTransfer = UsbManagerCreateTransfer(message, (struct usb_transfer*)transfer,
            transferId, deviceId);
    UsbTransferStatus_t status = HciQueueTransferGeneric(usbTransfer);
    if (status != TransferInProgress && status != TransferQueued) {
        UsbManagerDestroyTransfer(usbTransfer);
        ctt_usbhost_queue_response(message, status, 0);
    }
}

void ctt_usbhost_queue_periodic_invocation(struct gracht_message* message, const uuid_t processId,
                                           const uuid_t deviceId, const uuid_t transferId, const uint8_t* transfer, const uint32_t transfer_count)
{
    UsbManagerTransfer_t* usbTransfer = UsbManagerCreateTransfer(message, (struct usb_transfer*)transfer,
            transferId, deviceId);
    UsbTransferStatus_t status;

    if (usbTransfer->Transfer.Type == USB_TRANSFER_ISOCHRONOUS) {
        status = HciQueueTransferIsochronous(usbTransfer);
    } else {
        status = HciQueueTransferGeneric(usbTransfer);
    }

    if (status != TransferInProgress && status != TransferQueued) {
        UsbManagerDestroyTransfer(usbTransfer);
    }
    ctt_usbhost_queue_periodic_response(message, status);
}

void ctt_usbhost_reset_periodic_invocation(struct gracht_message* message, const uuid_t processId,
                                           const uuid_t deviceId, const uuid_t transferId)
{
    oserr_t                 status     = OS_ENOENT;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    UsbManagerTransfer_t*   transfer   = NULL;

    // Lookup transfer by iterating through available transfers
    foreach(node, &controller->TransactionList) {
        UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->value;
        if (itr->DeferredMessage[0].client == message->client &&
            itr->Id == transferId) {
            transfer = itr;
            break;
        }
    }

    if (transfer != NULL) {
        UsbManagerIterateChain(controller, transfer->EndpointDescriptor,
                               USB_CHAIN_DEPTH, USB_REASON_RESET, HciProcessElement, transfer);
        HciProcessEvent(controller, USB_EVENT_RESTART_DONE, transfer);
        transfer->Status = TransferInProgress;
        transfer->Flags  = TransferFlagNone;
        status = OS_EOK;
    }

    ctt_usbhost_reset_periodic_response(message, status);
}

void ctt_usbhost_dequeue_invocation(struct gracht_message* message, const uuid_t processId,
                                    const uuid_t deviceId, const uuid_t transferId)
{
    oserr_t                 status     = OS_ENOENT;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    UsbManagerTransfer_t*   transfer   = NULL;

    // Lookup transfer by iterating through available transfers
    foreach(node, &controller->TransactionList) {
        UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->value;
        if (itr->DeferredMessage[0].client == message->client &&
            itr->Id == transferId) {
            transfer = itr;
            break;
        }
    }

    // Dequeue and send result back
    if (transfer != NULL) {
        status = HciDequeueTransfer(transfer);
    }
    
    ctt_usbhost_dequeue_response(message, status);
}
