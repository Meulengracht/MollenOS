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

#ifndef __USB_COMMON_TRANSFER__
#define __USB_COMMON_TRANSFER__

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

enum USBManagerTransferState {
    USBTRANSFER_STATE_CREATED,
    USBTRANSFER_STATE_WAITING,
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
    
    union {
        struct {
            size_t BytesTransferred;
        } Async;
        struct {
            size_t   CurrentDataIndex;
            uint32_t PeriodicBufferSize;
            uint8_t  PeriodicBandwith;
            uint8_t  PeriodicInterval;
        } Periodic;
    } TData;

    // SHMHandles are references we must keep on the underlying
    // physical memory that we use to transfer data in/out of.
    OSHandle_t SHMHandles[USB_TRANSACTIONCOUNT];

    // Deferred message for async responding
    struct gracht_message DeferredMessage[];
} UsbManagerTransfer_t;

/**
 * @brief Intended for use when resetting periodic transfers. Resets
 * state and status to vales expected when newly queued. Removes any state
 * flags.
 */
static inline void __Transfer_Reset(UsbManagerTransfer_t* transfer) {
    transfer->Status = TransferOK;
    transfer->State  = USBTRANSFER_STATE_QUEUED;
    transfer->Flags  &= ~(__USBTRANSFER_FLAG_NOTIFIED | __USBTRANSFER_FLAG_SHORT);
}

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
 * @brief Returns whether the transfer has completed its transfer elements, or
 * experienced an error.
 * It may be deemed completed even with only partially executed elements if
 * the transfer was marked as SHORT.
 * This is only valid when the transfer is async.
 */
static inline bool __Transfer_IsComplete(UsbManagerTransfer_t* transfer) {
    if (transfer->Status != TransferOK) {
        return true;
    }
    if (transfer->ElementsCompleted == transfer->ElementCount) {
        return true;
    }
    if (transfer->Flags & __USBTRANSFER_FLAG_SHORT) {
        return true;
    }
    return false;
}

/**
 * @brief Cleans up a usb transfer and frees resources.
 * @param transfer 
 */
extern void
UsbManagerDestroyTransfer(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Notifies the waiting/subscribing process of transfer completion.
 * The transfer may have completed in error state or succesfully.
 * @param transfer 
 */
extern void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t* transfer);

#endif //!__USB_COMMON_TRANSFER__
 