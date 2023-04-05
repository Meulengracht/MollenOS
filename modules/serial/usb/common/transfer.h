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

#ifndef __USB_TRANSFER__
#define __USB_TRANSFER__

#include <usb/usb.h>
#include <gracht/link/vali.h>
#include <os/types/handle.h>
#include <os/types/shm.h>
#include <os/spinlock.h>
#include <threads.h>

#define __USBTRANSFER_FLAG_SHORT       0x1
#define __USBTRANSFER_FLAG_SYNC        0x2
#define __USBTRANSFER_FLAG_NOTIFIED    0x4
#define __USBTRANSFER_FLAG_PARTIAL     0x8
#define __USBTRANSFER_FLAG_SILENT      0x10
#define __USBTRANSFER_FLAG_FAILONSHORT 0x20
enum UsbManagerTransferFlags {
    TransferFlagNone        = 0,
    TransferFlagShort       = 0x1,
    TransferFlagSync        = 0x2,
    TransferFlagSchedule    = 0x4,
    TransferFlagUnschedule  = 0x8,
    TransferFlagCleanup     = 0x10,
    TransferFlagNotified    = 0x20,
    TransferFlagPartial     = 0x40,
    TransferFlagSilent = 0x80,
    TransferFlagFailOnShort = 0x100
};

enum USBManagerTransferState {
    USBTRANSFER_STATE_CREATED,
    USBTRANSFER_STATE_SCHEDULE,
    USBTRANSFER_STATE_QUEUED,
    USBTRANSFER_STATE_UNSCHEDULE,
    USBTRANSFER_STATE_CLEANUP,
    USBTRANSFER_STATE_COMPLETED,
};

struct TransferElement {
    enum USBTransactionType Type;
    uintptr_t               DataAddress;
    uint32_t                Length;
};

/**
 * An USB Transfer can consist of up to 3 transactions. This is done
 * to easily enable transfer as control and bulk that usually consists
 * of up to 3 steps.
 */
typedef struct UsbManagerTransfer {
    element_t ListHeader;

    // Transfer Metadata
    uuid_t                       ID;
    uuid_t                       DeviceID;
    enum USBTransferType         Type;
    USBAddress_t                 Address;
    uint16_t                     MaxPacketSize;
    enum USBManagerTransferState State;
    enum USBTransferCode         Status;
    unsigned int                 Flags;
    void*                        EndpointDescriptor;

    struct TransferElement* Elements;
    int                     ElementCount;
    int                     ElementsCompleted;
    size_t                  BytesTransferred;

    // Periodic Transfers
    size_t CurrentDataIndex;

    // SHMHandles are references we must keep on the underlying
    // physical memory that we use to transfer data in/out of.
    OSHandle_t SHMHandles[USB_TRANSACTIONCOUNT];

    // Deferred message for async responding
    struct gracht_message DeferredMessage[];
} UsbManagerTransfer_t;

/**
 * @brief Returns true if the transfer is either control or bulk
 */
static inline bool __Transfer_IsAsync(UsbManagerTransfer_t* transfer) {
    return transfer->Type == USBTRANSFER_TYPE_CONTROL ||
        transfer->Type == USBTRANSFER_TYPE_BULK;
}

/**
 * @brief Returns true if the transfer is either control or bulk
 */
static inline bool __Transfer_IsPeriodic(UsbManagerTransfer_t* transfer) {
    return transfer->Type == USBTRANSFER_TYPE_INTERRUPT ||
           transfer->Type == USBTRANSFER_TYPE_ISOC;
}

/**
 * UsbManagerDestroyTransfer
 * * Cleans up a usb transfer and frees resources.
 */
__EXTERN void
UsbManagerDestroyTransfer(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * UsbManagerSendNotification
 * * Sends a notification to the subscribing process whenever a periodic
 * * transaction has completed/failed. */
__EXTERN void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t* transfer);

#endif //!__USB_TRANSFER__
 