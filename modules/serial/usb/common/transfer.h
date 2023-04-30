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
#define __USBTRANSFER_FLAG_NOTIFIED    0x2
#define __USBTRANSFER_FLAG_SILENT      0x4
#define __USBTRANSFER_FLAG_FAILONSHORT 0x8

enum USBManagerTransferState {
    USBTRANSFER_STATE_CREATED,
    USBTRANSFER_STATE_WAITING,
    USBTRANSFER_STATE_SCHEDULE,
    USBTRANSFER_STATE_QUEUED,
    USBTRANSFER_STATE_UNSCHEDULE,
    USBTRANSFER_STATE_CLEANUP,
    USBTRANSFER_STATE_COMPLETED,
};

enum TransferElementType {
    TRANSFERELEMENT_TYPE_SETUP,
    TRANSFERELEMENT_TYPE_IN,
    TRANSFERELEMENT_TYPE_OUT
};

struct TransferElement {
    enum TransferElementType Type;
    uint32_t                 Length;
    union {
        // DataAddress holds the physical start address of the
        // transfer element buffer. This is only used for UHCI
        // and OHCI descritpors that do not support SG.
        uintptr_t Address;
        struct {
            uintptr_t Page0;
            uintptr_t Page1;
            uint32_t  Offsets[8];
        } OHCI;
        struct {
            // Isoc TDs hold up to 8 transactions
            // Normal TDs hold up to 5 transactions
            uintptr_t Addresses[8];
            uint32_t  Lengths[8];
        } EHCI;
    } Data;
};

static inline enum TransferElementType __TransferElement_DirectionToType(enum USBTransferDirection direction) {
    switch (direction) {
        case USBTRANSFER_DIRECTION_IN:
            return TRANSFERELEMENT_TYPE_IN;
        default:
            return TRANSFERELEMENT_TYPE_OUT;
    }
}

typedef struct UsbManagerTransfer {
    element_t ListHeader;

    // Transfer Metadata
    uuid_t                       ID;
    uuid_t                       DeviceID;
    enum USBTransferType         Type;
    enum USBSpeed                Speed;
    enum USBTransferDirection    Direction;
    USBAddress_t                 Address;
    uint16_t                     MaxPacketSize;
    enum USBManagerTransferState State;
    unsigned int                 Flags;
    void*                        RootElement;
    int                          ChainLength;

    struct TransferElement* Elements;
    int                     ElementCount;

    // ResultCode was the result of the transfer. It must
    // be updated once the transfer has been determined to
    // be either in error state or completed.
    enum USBTransferCode ResultCode;
    union {
        struct {
            uint32_t BytesTransferred;
            // ElementsCompleted tracks the number of elements that has
            // been completed to support partial transfers. This is only
            // valid for asynchronous transfers, and should *not* be used for
            // periodic transfers.
            int ElementsCompleted;
        } Async;
        struct {
            uint32_t CurrentDataIndex;
            uint32_t BufferSize;
            uint8_t  Bandwith;
            uint8_t  Interval;
        } Periodic;
    } TData;

    // SHMHandle is the reference we must keep on the underlying
    // physical memory that we use to transfer data in/out of.
    OSHandle_t SHMHandle;

    // Deferred message for async responding
    struct gracht_message DeferredMessage[];
} UsbManagerTransfer_t;

/**
 * @brief Intended for use when resetting periodic transfers. Resets
 * state and status to vales expected when newly queued. Removes any state
 * flags.
 */
static inline void __Transfer_Reset(UsbManagerTransfer_t* transfer) {
    transfer->ResultCode = USBTRANSFERCODE_INVALID;
    transfer->State      = USBTRANSFER_STATE_QUEUED;
    transfer->Flags     &= ~(__USBTRANSFER_FLAG_NOTIFIED | __USBTRANSFER_FLAG_SHORT);
}

/**
 * @brief Returns true if the transfer is either control or bulk
 */
static inline bool __Transfer_IsAsync(UsbManagerTransfer_t* transfer) {
    return transfer->Type == USBTRANSFER_TYPE_CONTROL ||
        transfer->Type == USBTRANSFER_TYPE_BULK;
}

/**
 * @brief Returns true if the transfer is either interrupt or isoc
 */
static inline bool __Transfer_IsPeriodic(UsbManagerTransfer_t* transfer) {
    return transfer->Type == USBTRANSFER_TYPE_INTERRUPT ||
        transfer->Type == USBTRANSFER_TYPE_ISOC;
}

/**
 * @brief Returns whether the transfer has completed its transfer elements, or
 * experienced an error.
 * This is only valid when the transfer is async.
 */
static inline bool __Transfer_IsPartial(UsbManagerTransfer_t* transfer) {
    // If the transfer was short, then it cannot be partially done as short
    // indicates no more data available.
    if (transfer->Flags & __USBTRANSFER_FLAG_SHORT) {
        return false;
    }
    return transfer->TData.Async.ElementsCompleted != transfer->ElementCount;
}

/**
 * @brief Calculates the total length of the transfer based on the transfer
 * elements initialized.
 * @param transfer The transfer to calculate length for.
 * @return The total transfer length.
 */
static inline uint32_t __Transfer_Length(UsbManagerTransfer_t* transfer) {
    uint32_t length = 0;
    for (int i = 0; i < transfer->ElementCount; i++) {
        length += transfer->Elements[i].Length;
    }
    return length;
}

/**
 * @brief Returns the transaction type of the transfer. This only checks the first
 * transaction type, and can only be used for non-control transfers.
 * @param transfer The transfer to retrieve the transaction type for.
 * @return The transaction type of the transfer.
 */
static inline enum USBTransferDirection __Transfer_Direction(UsbManagerTransfer_t* transfer) {
    return transfer->Direction;
}

/**
 * @brief Cleans up a usb transfer and frees resources.
 * @param transfer 
 */
extern void
USBTransferDestroy(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Notifies the waiting/subscribing process of transfer completion.
 * The transfer may have completed in error state or succesfully.
 * @param transfer 
 */
extern void
USBTransferNotify(
    _In_ UsbManagerTransfer_t* transfer);

#endif //!__USB_COMMON_TRANSFER__
 