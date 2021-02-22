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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */

//#define __TRACE
#define __COMPILE_ASSERT

#include <assert.h>
#include <ddk/utils.h>
#include "hci.h"
#include <stdlib.h>
#include "transfer.h"

#include "ctt_driver_protocol_server.h"
#include "ctt_usbhost_protocol_server.h"

UsbManagerTransfer_t*
UsbManagerCreateTransfer(
    _In_ struct gracht_recv_message* message,
    _In_ UsbTransfer_t*              transfer,
    _In_ UUId_t                      transferId,
    _In_ UUId_t                      deviceId)
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

    usbTransfer = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t) + VALI_MSG_DEFER_SIZE(message));
    if (!usbTransfer) {
        return NULL;
    }
    
    memset(usbTransfer, 0, sizeof(UsbManagerTransfer_t));

    memcpy(&usbTransfer->Transfer, transfer, sizeof(UsbTransfer_t));
    gracht_vali_message_defer_response(&usbTransfer->DeferredMessage, message);

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
        }
        else if (i == 2 && usbTransfer->Transfer.Transactions[i].BufferHandle ==
                usbTransfer->Transfer.Transactions[i - 2].BufferHandle) {
            memcpy(&usbTransfer->Transactions[i], &usbTransfer->Transactions[i - 2],
                sizeof(struct UsbManagerTransaction));
        }
        else {
            dma_attach(usbTransfer->Transfer.Transactions[i].BufferHandle,
                &usbTransfer->Transactions[i].DmaAttachment);
            dma_get_sg_table(&usbTransfer->Transactions[i].DmaAttachment,
                &usbTransfer->Transactions[i].DmaTable, -1);
        }
        dma_sg_table_offset(&usbTransfer->Transactions[i].DmaTable,
            usbTransfer->Transfer.Transactions[i].BufferOffset,
            &usbTransfer->Transactions[i].SgIndex,
            &usbTransfer->Transactions[i].SgOffset);
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
        }
        else if (i == 2 && transfer->Transfer.Transactions[i].BufferHandle ==
                           transfer->Transfer.Transactions[i - 2].BufferHandle) {
            // do nothing, we already freed
        }
        else {
            free(transfer->Transactions[i].DmaTable.entries);
            dma_detach(&transfer->Transactions[i].DmaAttachment);
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
        ctt_usbhost_queue_response(&transfer->DeferredMessage.recv_message, transfer->Status, bytesTransferred);
    }
    else if (transfer->Transfer.Type == USB_TRANSFER_INTERRUPT) {
        ctt_usbhost_event_transfer_status_single(transfer->DeferredMessage.recv_message.client,
                                              transfer->Id, transfer->Status, transfer->CurrentDataIndex);
        if (transfer->Status == TransferFinished) {
            transfer->CurrentDataIndex = ADDLIMIT(0, transfer->CurrentDataIndex,
                                                  transfer->Transfer.Transactions[0].Length,
                                                  transfer->Transfer.PeriodicBufferSize);
        }
    }
    else {
        WARNING("UsbManagerSendNotification ISOCHRONOUS WHAT TO DO");
    }
}

void ctt_usbhost_queue_async_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_async_args* args)
{
    UsbManagerTransfer_t* transfer = UsbManagerCreateTransfer(message, args->transfer, args->transfer_id, args->device_id);
    UsbTransferStatus_t   status   = HciQueueTransferGeneric(transfer);
    if (status != TransferInProgress && status != TransferQueued) {
        UsbManagerDestroyTransfer(transfer);
        ctt_usbhost_event_transfer_status_single(message->client, args->transfer_id, status, 0);
    }
}

void ctt_usbhost_queue_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_args* args)
{
    UsbManagerTransfer_t* transfer = UsbManagerCreateTransfer(message, args->transfer, args->transfer_id, args->device_id);
    UsbTransferStatus_t   status   = HciQueueTransferGeneric(transfer);
    if (status != TransferInProgress && status != TransferQueued) {
        UsbManagerDestroyTransfer(transfer);
        ctt_usbhost_queue_response(message, status, 0);
    }
}

void ctt_usbhost_queue_periodic_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_periodic_args* args)
{
    UsbManagerTransfer_t* transfer = UsbManagerCreateTransfer(message, args->transfer, args->transfer_id, args->device_id);
    UsbTransferStatus_t   status;

    if (transfer->Transfer.Type == USB_TRANSFER_ISOCHRONOUS) {
        status = HciQueueTransferIsochronous(transfer);
    }
    else {
        status = HciQueueTransferGeneric(transfer);
    }

    if (status != TransferInProgress && status != TransferQueued) {
        UsbManagerDestroyTransfer(transfer);
    }
    ctt_usbhost_queue_periodic_response(message, status);
}

void ctt_usbhost_reset_periodic_callback(
        _In_ struct gracht_recv_message*             message,
        _In_ struct ctt_usbhost_reset_periodic_args* args)
{
    OsStatus_t              status     = OsDoesNotExist;
    UsbManagerController_t* controller = UsbManagerGetController(args->device_id);
    UsbManagerTransfer_t*   transfer   = NULL;

    // Lookup transfer by iterating through available transfers
    foreach(node, &controller->TransactionList) {
        UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->value;
        if (itr->DeferredMessage.recv_message.client == message->client &&
            itr->Id == args->transfer_id) {
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
        status = OsSuccess;
    }

    ctt_usbhost_reset_periodic_response(message, status);
}

void ctt_usbhost_dequeue_callback(struct gracht_recv_message* message, struct ctt_usbhost_dequeue_args* args)
{
    OsStatus_t              status     = OsDoesNotExist;
    UsbManagerController_t* controller = UsbManagerGetController(args->device_id);
    UsbManagerTransfer_t*   transfer   = NULL;

    // Lookup transfer by iterating through available transfers
    foreach(node, &controller->TransactionList) {
        UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->value;
        if (itr->DeferredMessage.recv_message.client == message->client &&
            itr->Id == args->transfer_id) {
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
