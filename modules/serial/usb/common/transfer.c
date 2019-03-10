/* MollenOS
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

#include <os/mollenos.h>
#include <ddk/utils.h>
#include "transfer.h"
#include <assert.h>
#include <stdlib.h>

static UUId_t __GlbTransferId   = 0;

/* UsbManagerCreateTransfer
 * Creates a new transfer with the usb-manager.
 * Identifies and registers with neccessary services */
UsbManagerTransfer_t*
UsbManagerCreateTransfer(
    _In_ UsbTransfer_t*         Transfer,
    _In_ MRemoteCallAddress_t*  Address,
    _In_ UUId_t                 Device)
{
    UsbManagerTransfer_t *UsbTransfer = NULL;

    // Allocate a new instance
    UsbTransfer = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t));
    memset(UsbTransfer, 0, sizeof(UsbManagerTransfer_t));

    // Copy information over
    memcpy(&UsbTransfer->Transfer, Transfer, sizeof(UsbTransfer_t));
    memcpy(&UsbTransfer->ResponseAddress, Address, sizeof(MRemoteCallAddress_t));
    UsbTransfer->DeviceId = Device;
    UsbTransfer->Id       = __GlbTransferId++;
    UsbTransfer->Status   = TransferNotProcessed;
    return UsbTransfer;
}

/* UsbManagerSendNotification 
 * Sends a notification to the subscribing process whenever a periodic
 * transaction has completed/failed. */
void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t*  Transfer)
{
    // Variables
    UsbTransferResult_t Result;
    
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
        Result.BytesTransferred = Transfer->BytesTransferred[0];
        Result.BytesTransferred += Transfer->BytesTransferred[1];
        Result.BytesTransferred += Transfer->BytesTransferred[2];

        Result.Status           = Transfer->Status;
        RPCRespond(&Transfer->ResponseAddress, (void*)&Result, sizeof(UsbTransferResult_t));
    }
    else {
        // Send interrupt
        InterruptDriver(
            Transfer->ResponseAddress.Process,          // Process
            (size_t)Transfer->Transfer.PeriodicData,    // Data pointer 
            Transfer->Status,                           // Status of transfer
            Transfer->CurrentDataIndex, 0);             // Data offset (not used in isoc)

        // Increase
        if (Transfer->Transfer.Type == InterruptTransfer) {
            Transfer->CurrentDataIndex = ADDLIMIT(0, Transfer->CurrentDataIndex,
                Transfer->Transfer.Transactions[0].Length, Transfer->Transfer.PeriodicBufferSize);
        }
    }
}
