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
 * MollenOS MCore - USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */

#ifndef __USB_TRANSFER__
#define __USB_TRANSFER__

/* Includes
 * - Library */
#include <os/contracts/usbhost.h>
#include <os/spinlock.h>
#include <os/osdefs.h>
#include <os/usb.h>

/* UsbManagerTransferFlags
 * Describes a unified way of reporting special transfer states. */
typedef enum _UsbManagerTransferFlags {
    TransferFlagNone        = 0x0,
    TransferFlagShort       = 0x1,
    TransferFlagNAK         = 0x2,
    TransferFlagSync        = 0x4,
    TransferFlagSchedule    = 0x8,
    TransferFlagUnschedule  = 0x10,
    TransferFlagCleanup     = 0x20,
} UsbManagerTransferFlags_t;

/* UsbManagerTransfer
 * Describes a generic transfer with information needed
 * in order to execute a callback for the requester */
typedef struct _UsbManagerTransfer {
    UsbTransfer_t               Transfer;
    UUId_t                      Requester;
    int                         ResponsePort;

    // Transfer Metadata
    UUId_t                      Id;
    UUId_t                      DeviceId;
    UUId_t                      Pipe;
    UsbTransferStatus_t         Status;
    UsbManagerTransferFlags_t   Flags;

    // Control/Interrupt transfers are small, but carry data.
    // Information here is shared
    void*                       EndpointDescriptor;  // We only use one no matter what
    int                         TransactionsExecuted;
    int                         TransactionsTotal;
    size_t                      BytesTransferred[USB_TRANSACTIONCOUNT]; // In Total
    size_t                      CurrentDataIndex;    // Periodic Transfers
} UsbManagerTransfer_t;

/* UsbManagerCreateTransfer
 * Creates a new transfer with the usb-manager.
 * Identifies and registers with neccessary services */
__EXTERN
UsbManagerTransfer_t*
UsbManagerCreateTransfer(
    _In_ UsbTransfer_t*         Transfer,
    _In_ UUId_t                 Requester,
    _In_ int                    ResponsePort,
    _In_ UUId_t                 Device,
    _In_ UUId_t                 Pipe);

/* UsbManagerSendNotification
 * Sends a notification to the subscribing process whenever a periodic
 * transaction has completed/failed. */
__EXTERN
void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t*  Transfer);

#endif //!__USB_TRANSFER__
 