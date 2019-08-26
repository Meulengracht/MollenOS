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
#include <os/mollenos.h>
#include <os/ipc.h>
#include <stdlib.h>
#include "transfer.h"

static UUId_t __GlbTransferId = 0;

UsbManagerTransfer_t*
UsbManagerCreateTransfer(
    _In_ UsbTransfer_t* Transfer,
    _In_ thrd_t         Address,
    _In_ UUId_t         DeviceId)
{
    UsbManagerTransfer_t* UsbTransfer;
    int                   i;

    UsbTransfer = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t));
    if (!UsbTransfer) {
        return NULL;
    }
    
    memset(UsbTransfer, 0, sizeof(UsbManagerTransfer_t));
    memcpy(&UsbTransfer->Transfer, Transfer, sizeof(UsbTransfer_t));
    
    UsbTransfer->Address  = Address;
    UsbTransfer->DeviceId = DeviceId;
    UsbTransfer->Id       = __GlbTransferId++;
    UsbTransfer->Status   = TransferNotProcessed;
    
    // When attaching to dma buffers make sure we don't attach
    // multiple times as we can then save a system call or two
    for (i = 0; i < UsbTransfer->Transfer.TransactionCount; i++) {
        if (UsbTransfer->Transfer.Transactions[i].BufferHandle == UUID_INVALID) {
            continue;
        }
        
        TRACE("... acquiring buffer for transaction %i (handle 0x%x, offset 0x%x)",
            i, LODWORD(UsbTransfer->Transfer.Transactions[i].BufferHandle),
            LODWORD(UsbTransfer->Transfer.Transactions[i].BufferOffset));
        if (i != 0 && UsbTransfer->Transfer.Transactions[i].BufferHandle ==
                UsbTransfer->Transfer.Transactions[i - 1].BufferHandle) {
            TRACE("... reusing");
            memcpy(&UsbTransfer->Transactions[i], &UsbTransfer->Transactions[i - 1],
                sizeof(struct UsbManagerTransaction));
        }
        else if (i == 2 && UsbTransfer->Transfer.Transactions[i].BufferHandle ==
                UsbTransfer->Transfer.Transactions[i - 2].BufferHandle) {
            TRACE("... reusing");
            memcpy(&UsbTransfer->Transactions[i], &UsbTransfer->Transactions[i - 2],
                sizeof(struct UsbManagerTransaction));
        }
        else {
            dma_attach(UsbTransfer->Transfer.Transactions[i].BufferHandle,
                &UsbTransfer->Transactions[i].DmaAttachment);
            dma_get_sg_table(&UsbTransfer->Transactions[i].DmaAttachment,
                &UsbTransfer->Transactions[i].DmaTable, -1);
        }
        dma_sg_table_offset(&UsbTransfer->Transactions[i].DmaTable,
            UsbTransfer->Transfer.Transactions[i].BufferOffset,
            &UsbTransfer->Transactions[i].SgIndex,
            &UsbTransfer->Transactions[i].SgOffset);
        TRACE("... sg_index %i, sg_offset %u", 
            UsbTransfer->Transactions[i].SgIndex,
            LODWORD(UsbTransfer->Transactions[i].SgOffset));
    }
    return UsbTransfer;
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
    UsbTransferResult_t Result;
    IpcMessage_t        Message = { 0 };
    
    TRACE("UsbManagerSendNotification()");
    
    // If user doesn't want, ignore
    if (Transfer->Transfer.Flags & USB_TRANSFER_NO_NOTIFICATION) {
        return;
    }

    // If notification has been sent on control/bulk do not send again
    if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
        if ((Transfer->Flags & TransferFlagNotified)) {
            return;
        }
        Transfer->Flags         |= TransferFlagNotified;
        Result.Id               = Transfer->Id;
        Result.BytesTransferred = Transfer->Transactions[0].BytesTransferred;
        Result.BytesTransferred += Transfer->Transactions[1].BytesTransferred;
        Result.BytesTransferred += Transfer->Transactions[2].BytesTransferred;

        Result.Status  = Transfer->Status;
        Message.Sender = Transfer->Address;
        IpcReply(&Message, &Result, sizeof(UsbTransferResult_t));
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
