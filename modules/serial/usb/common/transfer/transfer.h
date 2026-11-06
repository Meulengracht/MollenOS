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
#include <ddk/dma.h>
#include <ds/list.h>

#include "bounce.h"

#define __XPAGE(_a) ((_a) & ~((uintptr_t)0xFFF))
#define __XPAGEOFFSET(_a) ((_a) & 0xFFF)
#define __XUPPAGE(_a) (__XPAGE(_a) + 0x1000)

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
    USBTRANSFER_STATE_STALLED,
    USBTRANSFER_STATE_FINISHING,
    USBTRANSFER_STATE_RESTART,
};

enum TransferElementType {
    TRANSFERELEMENT_TYPE_SETUP,
    TRANSFERELEMENT_TYPE_IN,
    TRANSFERELEMENT_TYPE_OUT
};

/**
 * @brief Transfer length, direction and buffer addresses used to build HCI descriptors.
 */
struct TransferElement {
    enum TransferElementType Type;
    uint32_t                 Length;
    // The packet crosses device segments and needs one contiguous bounce slot.
    bool                     NeedsBounce;
    union {
        // Physical buffer address for UHCI and OHCI descriptors without SG support.
        uintptr_t Address;
        struct {
            uintptr_t Page0;
            uintptr_t Page1;
            uint32_t  Offsets[8];
        } OHCI;
        struct {
            // Isoc TDs hold up to 8 transactions.
            // Normal TDs hold up to 5 transactions.
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

/**
 * @brief Private packet buffer and its offset in the client buffer.
 */
typedef struct UsbTransferDmaBounce {
    OSHandle_t Buffer;
    size_t     ClientOffset;
} UsbTransferDmaBounce_t;

/**
 * @brief Mapping and packet settings used to build the current transfer elements.
 */
typedef struct UsbTransferDmaLayout {
    enum USBTransferType      Type;
    enum USBTransferDirection Direction;
    uint16_t                  MaxPacketSize;
    uint32_t                  Length;
    struct DMAMapping*        Mapping;
} UsbTransferDmaLayout_t;

typedef struct UsbTransferWork {
    element_t Header;
    list_t*   Queue;
} UsbTransferWork_t;

/** Bounded tracing for a selected enumeration request and its DMA handoff. */
struct UsbTransferDmaDiagnostics {
    bool         Enabled;
    bool         EndpointSampled;
    unsigned int Preparations;
    unsigned int Submissions;
    unsigned int Completions;
    unsigned int Events;
};

/**
 * @brief Transfer state, HCI descriptors, DMA mappings and client completion data.
 */
typedef struct UsbManagerTransfer {
    element_t ListHeader;

    // Transfer metadata.
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
    bool                         Submitting; // An RPC is using this transfer across a yielding call.

    // HCI descriptors kept between periodic submissions.
    // HCITransferFinalize releases them when the transfer is cancelled.
    struct UsbTransferWork  Work;
    void*                   ReusableElements;

    struct TransferElement* Elements;
    int                     ElementCount;
    int                     ElementCapacity;

    // Update ResultCode when the transfer completes or fails.
    enum USBTransferCode ResultCode;
    union {
        struct {
            uint32_t BytesTransferred;
            // Completed element count for partial asynchronous transfers.
            // Only valid for control and bulk transfers.
            int ElementsCompleted;
        } Async;
        struct {
            uint32_t CurrentDataIndex;
            uint32_t BufferSize;
            uint8_t  Bandwith; // Additional transactions per service interval.
            uint8_t  Interval;
        } Periodic;
    } TData;

    // The client attachment allows CPU access for setup and packet bounce.
    // The DMA mapping keeps the storage allocated until the mapping is released.
    OSHandle_t SHMHandle;

    // Device addresses and DMA transfer state.
    // Small periodic packets reuse one private bounce page. Larger periodic
    // packets get a mapping for each client slot when that slot is first used.
    // If Mappings is present, Mapping points to the current slot in that array.
    // Otherwise, Mapping is released separately. The CPU can access completed
    // client slots while the next packet is in flight.
    struct {
        struct DMAContext*               Context;
        struct DMAMapping*               Mapping;
        struct DMAMapping**              Mappings;
        uint32_t                         MappingCount;
        const struct DMASegment*         Segments;
        uint32_t                         SegmentCount;
        size_t                           BufferOffset;
        uint32_t                         BufferLength;
        uint32_t                         Length;
        size_t                           Completed;
        uint32_t                         ReleaseEpoch;
        bool                             Unlinked;
        bool                             Linked; // Hardware schedule contains this descriptor chain.
        bool                             DeviceOwned;
        bool                             HardwareActive;
        usb_packet_t                     Setup;
        struct UsbTransferDmaBounce     Bounce;
        struct UsbTransferBouncePool*   PacketPool;
        struct UsbTransferPacketBounce* Packets;
        struct UsbTransferDmaLayout      Layout;
        struct UsbTransferDmaDiagnostics Diagnostics;
    } Dma;

    // Saved message for the asynchronous response.
    struct gracht_message DeferredMessage[];
} UsbManagerTransfer_t;

/**
 * @brief Prepare the mapping for device access before publishing descriptors.
 * Partial asynchronous submissions continue using the current DMA transfer.
 *
 * @param transfer Transfer with a prepared mapping and descriptors.
 * @return OS_EOK on success, otherwise the DMA start error.
 */
extern oserr_t
USBTransferDmaBegin(
    _InOut_ UsbManagerTransfer_t* transfer);

/**
 * @brief Locate a byte offset within the selected mapping's device segments.
 *
 * @param transfer Transfer with a prepared DMA layout.
 * @param offset Byte offset relative to the start of that mapping.
 * @param indexOut Segment containing the byte.
 * @param offsetOut Byte offset within the segment.
 * @return OS_EOK on success, OS_EINVALPARAMS when the byte is outside the mapping.
 */
static inline oserr_t
USBTransferDmaOffset(
    _In_ const UsbManagerTransfer_t* transfer,
    _In_ size_t                      offset,
    _Out_ int*                       indexOut,
    _Out_ size_t*                    offsetOut)
{
    for (uint32_t i = 0; i < transfer->Dma.SegmentCount; i++) {
        if (offset < transfer->Dma.Segments[i].Length) {
            *indexOut = (int)i;
            *offsetOut = offset;
            return OS_EOK;
        }
        offset -= transfer->Dma.Segments[i].Length;
    }
    return OS_EINVALPARAMS;
}

/**
 * @brief Reset a periodic transfer to the queued state. Clears the result,
 * notification flag and short-transfer flag.
 *
 * @param transfer Periodic transfer to reset.
 */
static inline void __Transfer_Reset(UsbManagerTransfer_t* transfer) {
    transfer->ResultCode = USBTRANSFERCODE_INVALID;
    transfer->State      = USBTRANSFER_STATE_QUEUED;
    transfer->Flags     &= ~(__USBTRANSFER_FLAG_NOTIFIED | __USBTRANSFER_FLAG_SHORT);
}

/**
 * @brief Returns true for control and bulk transfers.
 */
static inline bool __Transfer_IsAsync(UsbManagerTransfer_t* transfer) {
    return transfer->Type == USBTRANSFER_TYPE_CONTROL ||
        transfer->Type == USBTRANSFER_TYPE_BULK;
}

/**
 * @brief Returns true for interrupt and isochronous transfers.
 */
static inline bool __Transfer_IsPeriodic(UsbManagerTransfer_t* transfer) {
    return transfer->Type == USBTRANSFER_TYPE_INTERRUPT ||
        transfer->Type == USBTRANSFER_TYPE_ISOC;
}

/**
 * @brief Check whether an asynchronous transfer has elements left to complete.
 * A short transfer has no more data and returns false.
 *
 * @param transfer Control or bulk transfer to check.
 */
static inline bool __Transfer_IsPartial(UsbManagerTransfer_t* transfer) {
    // A short transfer indicates that no more data is available.
    if (transfer->Flags & __USBTRANSFER_FLAG_SHORT) {
        return false;
    }
    return transfer->TData.Async.ElementsCompleted != transfer->ElementCount;
}

/**
 * @brief Calculate the total length of the initialized transfer elements.
 *
 * @param transfer Transfer with initialized elements.
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
 * @brief Return the data direction of a non-control transfer.
 *
 * @param transfer Non-control transfer to check.
 * @return The transfer direction.
 */
static inline enum USBTransferDirection __Transfer_Direction(UsbManagerTransfer_t* transfer) {
    return transfer->Direction;
}

/**
 * @brief Release a USB transfer and its resources after removing its descriptors.
 *
 * @param transfer Transfer to destroy.
 */
extern oserr_t
USBTransferDestroy(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Notify the waiting or subscribing process that the transfer completed.
 * Reports both successful transfers and errors.
 *
 * @param transfer Completed transfer to report.
 */
extern void
USBTransferNotify(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Select or create the current packet's mapping and reuse element storage.
 *
 * @param transfer Transfer available for CPU access, with unchanged buffer bounds.
 * @return OS_EOK, or a mapping/allocation error with resources kept for cleanup.
 */
extern oserr_t
USBTransferDmaPrepare(
    _InOut_ UsbManagerTransfer_t* transfer);

/**
 * @brief Release all packet mappings after completion or a confirmed hardware stop.
 * Failed releases leave the remaining resources in place so cleanup can be retried.
 *
 * @param transfer Transfer whose DMA access has ended.
 * @return OS_EOK on success, OS_EBUSY while DMA is active, or a release error.
 */
extern oserr_t
USBTransferDmaDestroy(
    _InOut_ UsbManagerTransfer_t* transfer);

/**
 * @brief Make completed bytes available to the CPU.
 * The transfer must have completed or the hardware must have stopped.
 *
 * @param transfer Transfer whose device access has finished.
 * @param bytes Number of completed bytes, at most the mapped transfer length.
 * @return OS_EOK on success, otherwise a length or DMA completion error.
 */
extern oserr_t
USBTransferDmaComplete(
    _InOut_ UsbManagerTransfer_t* transfer,
    _In_ size_t bytes);

#endif //!__USB_COMMON_TRANSFER__
