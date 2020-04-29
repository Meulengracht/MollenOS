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
#include <os/mollenos.h>
#include <stdlib.h>
#include "transfer.h"

#include "ctt_driver_protocol_server.h"
#include "ctt_usbhost_protocol_server.h"

static UUId_t __GlbTransferId = 0;

UsbManagerTransfer_t*
UsbManagerCreateTransfer(
    _In_ UsbTransfer_t*              transfer,
    _In_ struct gracht_recv_message* message,
    _In_ UUId_t                      deviceId)
{
    UsbManagerTransfer_t* usbTransfer;
    int                   i;
    
    TRACE("[usb_create_transfer] transactions %i",
        transfer->TransactionCount);
    
    usbTransfer = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t));
    if (!usbTransfer) {
        return NULL;
    }
    
    memset(usbTransfer, 0, sizeof(UsbManagerTransfer_t));
    memcpy(&usbTransfer->Transfer, transfer, sizeof(UsbTransfer_t));
    
    gracht_vali_message_defer_response(&usbTransfer->DeferredMessage, message);
    
    usbTransfer->DeviceId = deviceId;
    usbTransfer->Id       = __GlbTransferId++;
    usbTransfer->Status   = TransferNotProcessed;
    
    // When attaching to dma buffers make sure we don't attach
    // multiple times as we can then save a system call or two
    for (i = 0; i < usbTransfer->Transfer.TransactionCount; i++) {
        if (usbTransfer->Transfer.Transactions[i].BufferHandle == UUID_INVALID) {
            continue;
        }
        TRACE("[usb_create_transfer] %i: length %" PRIuIN ", b_id %u, b_offset %" PRIuIN,
            i, transfer->Transactions[i].Length,
            transfer->Transactions[i].BufferHandle,
            transfer->Transactions[i].BufferOffset);
        
        if (i != 0 && usbTransfer->Transfer.Transactions[i].BufferHandle ==
                usbTransfer->Transfer.Transactions[i - 1].BufferHandle) {
            TRACE("... reusing");
            memcpy(&usbTransfer->Transactions[i], &usbTransfer->Transactions[i - 1],
                sizeof(struct UsbManagerTransaction));
        }
        else if (i == 2 && usbTransfer->Transfer.Transactions[i].BufferHandle ==
                usbTransfer->Transfer.Transactions[i - 2].BufferHandle) {
            TRACE("... reusing");
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
        TRACE("[usb_create_transfer] sg_count %i, sg_index %i, sg_offset %u",
            usbTransfer->Transactions[i].DmaTable.count,
            usbTransfer->Transactions[i].SgIndex,
            LODWORD(usbTransfer->Transactions[i].SgOffset));
    }
    return usbTransfer;
}

void
UsbManagerDestroyTransfer(
    _In_ UsbManagerTransfer_t* Transfer)
{
    int i;
    for (i = 0; i < Transfer->Transfer.TransactionCount; i++) {
        if (Transfer->Transfer.Transactions[i].BufferHandle == UUID_INVALID) {
            continue;
        }
        
        if (i != 0 && Transfer->Transfer.Transactions[i].BufferHandle ==
                Transfer->Transfer.Transactions[i - 1].BufferHandle) {
            // do nothing, we already freed
        }
        else if (i == 2 && Transfer->Transfer.Transactions[i].BufferHandle ==
                Transfer->Transfer.Transactions[i - 2].BufferHandle) {
            // do nothing, we already freed
        }
        else {
            free(Transfer->Transactions[i].DmaTable.entries);
            dma_detach(&Transfer->Transactions[i].DmaAttachment);
        }
    }
    free(Transfer);
}

void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t* Transfer)
{
    UUId_t id;
    size_t bytesTransferred;
    
    TRACE("[usb] [manager] send notification");
    
    // If user doesn't want, ignore
    if (Transfer->Transfer.Flags & USB_TRANSFER_NO_NOTIFICATION) {
        return;
    }

    // If notification has been sent on control/bulk do not send again
    if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
        if ((Transfer->Flags & TransferFlagNotified)) {
            return;
        }
        Transfer->Flags |= TransferFlagNotified;
        id               = Transfer->Id;
        bytesTransferred = Transfer->Transactions[0].BytesTransferred;
        bytesTransferred += Transfer->Transactions[1].BytesTransferred;
        bytesTransferred += Transfer->Transactions[2].BytesTransferred;
        ctt_usbhost_queue_response(&Transfer->DeferredMessage.recv_message, Transfer->Status,
            bytesTransferred);
    }
    else {
        // Forward data to the driver
        // @todo
        //InterruptDriver(
        //    Transfer->ResponseAddress.Process,          // Process
        //    (size_t)Transfer->Transfer.PeriodicData,    // Data pointer 
        //    Transfer->Status,                           // Status of transfer
        //    Transfer->CurrentDataIndex, 0);             // Data offset (not used in isoc)

        // Increase
        if (Transfer->Transfer.Type == InterruptTransfer) {
            Transfer->CurrentDataIndex = ADDLIMIT(0, Transfer->CurrentDataIndex,
                Transfer->Transfer.Transactions[0].Length, Transfer->Transfer.PeriodicBufferSize);
        }
    }
}

void ctt_usbhost_queue_async_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_async_args* args)
{
    UsbManagerTransfer_t* transfer = UsbManagerCreateTransfer(args->transfer, message, args->device_id);
    UsbTransferStatus_t   status   = HciQueueTransferGeneric(transfer);
    if (status != OsSuccess) {
        ctt_usbhost_queue_async_response(message, transfer->Id, status, 0);
    }
}

void ctt_usbhost_queue_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_args* args)
{
    UsbManagerTransfer_t* transfer = UsbManagerCreateTransfer(args->transfer, message, args->device_id);
    UsbTransferStatus_t   status   = HciQueueTransferGeneric(transfer);
    if (status != OsSuccess) {
        ctt_usbhost_queue_response(message, status, 0);
    }
}

void ctt_usbhost_queue_periodic_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_periodic_args* args)
{
    UsbManagerTransfer_t* transfer = UsbManagerCreateTransfer(args->transfer, message, args->device_id);
    UsbTransferStatus_t   status;
    
    if (transfer->Transfer.Type == IsochronousTransfer) {
        status = HciQueueTransferIsochronous(transfer);
    }
    else {
        status = HciQueueTransferGeneric(transfer);
    }
    
    ctt_usbhost_queue_periodic_response(message, status);
}

void ctt_usbhost_dequeue_callback(struct gracht_recv_message* message, struct ctt_usbhost_dequeue_args* args)
{
    OsStatus_t              status     = OsDoesNotExist;
    UsbManagerController_t* controller = UsbManagerGetController(args->device_id);
    UsbManagerTransfer_t*   transfer   = NULL;

    // Lookup transfer by iterating through available transfers
    foreach(node, controller->TransactionList) {
        UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->Data;
        if (itr->Id == args->transfer_id) {
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
