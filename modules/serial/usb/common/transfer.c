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

//#define __TRACE
#define __need_static_assert

#include <assert.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/shm.h>
#include "hci.h"
#include "transfer.h"

#include "ctt_driver_service_server.h"
#include "ctt_usbhost_service_server.h"

extern gracht_server_t* __crt_get_module_server(void);

static void
__DiscardTransfer(
    _In_ UsbManagerTransfer_t* transfer)
{
    UsbManagerController_t* controller;

    if (USBTransferDestroy(transfer) == OS_EOK) {
        return;
    }

    // Creation and queue failures already send their own response. Keep the
    // indexed owner for cleanup retries without notifying the caller twice.
    transfer->Flags |= __USBTRANSFER_FLAG_SILENT;
    transfer->ResultCode = USBTRANSFERCODE_INVALID;
    transfer->State = USBTRANSFER_STATE_CLEANUP;
    controller = UsbManagerGetController(transfer->DeviceID);
    UsbManagerQueueTransferWork(controller, transfer);
}

static unsigned int
__ConvertFlags(
        _In_ unsigned int flags)
{
    unsigned int tflags = 0;
    if (flags & USB_TRANSFER_NO_NOTIFICATION) {
        tflags |= __USBTRANSFER_FLAG_SILENT;
    }
    if (flags & USB_TRANSFER_SHORT_NOT_OK) {
        tflags |= __USBTRANSFER_FLAG_FAILONSHORT;
    }
    return tflags;
}

static oserr_t
__AttachSGTable(
        _In_ UsbManagerTransfer_t* usbTransfer,
        _In_ USBTransfer_t*        transfer,
        _In_ SHMSGTable_t*         sgTable)
{
    oserr_t oserr = SHMAttach(
            transfer->BufferHandle,
            &usbTransfer->SHMHandle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = SHMGetSGTable(
            &usbTransfer->SHMHandle,
            sgTable,
            -1
    );
    // The transfer owns the attachment even if SG discovery fails. Its common
    // cleanup path keeps that owner when release needs another attempt.
    return oserr;
}

oserr_t
USBTransferCreate(
        _In_  struct gracht_message* message,
        _In_  USBTransfer_t*         transfer,
        _In_  uuid_t                 transferId,
        _In_  uuid_t                 deviceId,
        _Out_ UsbManagerTransfer_t** transferOut)
{
    UsbManagerController_t* controller;
    UsbManagerTransfer_t*   usbTransfer;
    SHMSGTable_t            sgTable;
    oserr_t                 oserr;
    
    TRACE("USBTransferCreate(transfer=0x%" PRIxIN ", message=0x%" PRIxIN ", deviceId=%u)",
        transfer, message, deviceId);

    controller = (UsbManagerController_t*)UsbManagerGetController(deviceId);
    if (!controller) {
        return OS_ENOENT;
    }

    usbTransfer = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!usbTransfer) {
        return OS_EOOM;
    }
    
    memset(usbTransfer, 0, sizeof(UsbManagerTransfer_t));
    gracht_server_defer_message(message, &usbTransfer->DeferredMessage[0]);

    ELEMENT_INIT(&usbTransfer->ListHeader, 0, usbTransfer);
    usbTransfer->ID = transferId;
    usbTransfer->DeviceID = deviceId;
    usbTransfer->Type = transfer->Type;
    usbTransfer->Speed = transfer->Speed;
    usbTransfer->Direction = transfer->Direction;
    memcpy(&usbTransfer->Address, &transfer->Address, sizeof(USBAddress_t));
    usbTransfer->MaxPacketSize = transfer->MaxPacketSize;
    usbTransfer->Flags = __ConvertFlags(transfer->Flags);

    // Publish ownership before acquiring resources: a failed rollback must
    // remain reachable by controller teardown and the cleanup worker.
    list_append(&controller->TransactionList, &usbTransfer->ListHeader);

    if (__Transfer_IsPeriodic(usbTransfer)) {
        usbTransfer->TData.Periodic.Bandwith = transfer->PeriodicBandwith;
        usbTransfer->TData.Periodic.Interval = transfer->PeriodicInterval;
    }

    if (controller->DmaContext != NULL) {
        if (transfer->MaxPacketSize == 0 || transfer->Length == 0) {
            __DiscardTransfer(usbTransfer);
            return OS_EINVALPARAMS;
        }

        usbTransfer->Dma.Context = controller->DmaContext;
        usbTransfer->Dma.BufferOffset = transfer->BufferOffset;
        usbTransfer->Dma.BufferLength = transfer->Length;
        if (__Transfer_IsPeriodic(usbTransfer)) {
            usbTransfer->TData.Periodic.BufferSize = transfer->Length;
        }

        oserr = SHMAttach(transfer->BufferHandle, &usbTransfer->SHMHandle);
        if (oserr == OS_EOK) {
            oserr = USBTransferDmaPrepare(usbTransfer);
        }
        if (oserr != OS_EOK) {
            __DiscardTransfer(usbTransfer);
            return oserr;
        }

        *transferOut = usbTransfer;
        return OS_EOK;
    }

    // Get the SG table before calculating TDs
    oserr = __AttachSGTable(usbTransfer, transfer, &sgTable);
    if (oserr != OS_EOK) {
        __DiscardTransfer(usbTransfer);
        return oserr;
    }

    // Count the needed number of transfer elements
    oserr = HCITransferElementsNeeded(
            usbTransfer,
            transfer->Length,
            transfer->Direction,
            &sgTable,
            transfer->BufferOffset,
            &usbTransfer->ElementCount
    );
    if (oserr != OS_EOK) {
        free(sgTable.Entries);
        __DiscardTransfer(usbTransfer);
        return oserr;
    }

    // Allocate the transfer element array
    usbTransfer->Elements = calloc(sizeof(struct TransferElement), usbTransfer->ElementCount);
    if (usbTransfer->Elements == NULL) {
        free(sgTable.Entries);
        __DiscardTransfer(usbTransfer);
        return OS_EOOM;
    }
    HCITransferElementFill(
            usbTransfer,
            transfer->Length,
            transfer->Direction,
            &sgTable,
            transfer->BufferOffset
    );
    free(sgTable.Entries);
    *transferOut = usbTransfer;
    return OS_EOK;
}

oserr_t
USBTransferDestroy(
    _In_ UsbManagerTransfer_t* transfer)
{
    UsbManagerController_t* controller;
    oserr_t                 status;

    if (!transfer) {
        return OS_EOK;
    }

    // A failed unmap leaves storage owned, including after a failed controller stop.
    status = USBTransferDmaDestroy(transfer);
    if (status != OS_EOK) {
        return status;
    }
    
    // Keep the transfer indexed until its client attachment is released too.
    // The manager can then retry cleanup without losing the last SHM owner.
    status = SHMRelease(&transfer->SHMHandle);
    if (status != OS_EOK) {
        WARNING("USB transfer SHM release failed (%u); keeping transfer", status);
        return status;
    }

    controller = (UsbManagerController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller) {
        list_remove(&controller->TransactionList, &transfer->ListHeader);
    }
    UsbManagerForgetTransferWork(transfer);
    free(transfer->Elements);
    free(transfer);
    return OS_EOK;
}

void
USBTransferNotify(
    _In_ UsbManagerTransfer_t* transfer)
{
    TRACE("USBTransferNotify(transfer=0x%" PRIxIN ")", transfer);

    // If user doesn't want, ignore
    TRACE("USBTransferNotify transfer type=%u", transfer->Type);
    if (transfer->Flags & __USBTRANSFER_FLAG_SILENT) {
        return;
    }

    // If notification has been sent on control/bulk do not send again
    if (__Transfer_IsAsync(transfer)) {
        if ((transfer->Flags & __USBTRANSFER_FLAG_NOTIFIED)) {
            return;
        }
        ctt_usbhost_queue_response(
                &transfer->DeferredMessage[0],
                OS_EOK,
                from_usbcode(transfer->ResultCode),
                transfer->TData.Async.BytesTransferred
        );
        transfer->Flags |= __USBTRANSFER_FLAG_NOTIFIED;
    } else if (transfer->Type == USBTRANSFER_TYPE_INTERRUPT) {
        ctt_usbhost_event_transfer_status_single(
                __crt_get_module_server(),
                transfer->DeferredMessage[0].client,
                transfer->ID,
                from_usbcode(transfer->ResultCode),
                transfer->TData.Periodic.CurrentDataIndex
        );
        if (transfer->ResultCode == USBTRANSFERCODE_SUCCESS) {
            transfer->TData.Periodic.CurrentDataIndex = ADDLIMIT(
                    0, transfer->TData.Periodic.CurrentDataIndex,
                    transfer->Dma.Context != NULL ? transfer->Dma.Length : transfer->Elements[0].Length,
                    transfer->TData.Periodic.BufferSize
            );
        }
    }
}

void ctt_usbhost_queue_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t deviceId,
                                  const uuid_t transferId, const uint8_t* transfer, const uint32_t transfer_count)
{
    UsbManagerTransfer_t* usbTransfer;
    oserr_t               oserr;

    oserr = USBTransferCreate(
            message,
            (USBTransfer_t*)transfer,
            transferId,
            deviceId,
            &usbTransfer
    );
    if (oserr != OS_EOK) {
        ctt_usbhost_queue_response(message, oserr, CTT_USB_TRANSFER_STATUS_NAK, 0);
        return;
    }

    oserr = HCITransferQueue(usbTransfer);
    if (oserr != OS_EOK) {
        __DiscardTransfer(usbTransfer);
        ctt_usbhost_queue_response(message, oserr, CTT_USB_TRANSFER_STATUS_NAK, 0);
    }
}

void ctt_usbhost_queue_periodic_invocation(struct gracht_message* message, const uuid_t processId,
                                           const uuid_t deviceId, const uuid_t transferId, const uint8_t* transfer, const uint32_t transfer_count)
{
    UsbManagerTransfer_t* usbTransfer;
    oserr_t               oserr;

    oserr = USBTransferCreate(
            message,
            (USBTransfer_t*)transfer,
            transferId,
            deviceId,
            &usbTransfer
    );
    if (oserr != OS_EOK) {
        ctt_usbhost_queue_periodic_response(message, oserr);
        return;
    }

    if (usbTransfer->Type == USBTRANSFER_TYPE_ISOC) {
        oserr = HCITransferQueueIsochronous(usbTransfer);
    } else {
        oserr = HCITransferQueue(usbTransfer);
    }

    if (oserr != OS_EOK) {
        __DiscardTransfer(usbTransfer);
    }
    ctt_usbhost_queue_periodic_response(message, oserr);
}

void ctt_usbhost_reset_periodic_invocation(struct gracht_message* message, const uuid_t processId,
                                           const uuid_t deviceId, const uuid_t transferId)
{
    oserr_t                 status     = OS_ENOENT;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    UsbManagerTransfer_t*   transfer   = NULL;

    // Lookup transfer by iterating through available transfers
    if (controller != NULL) {
        foreach(node, &controller->TransactionList) {
            UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->value;
            if (itr->DeferredMessage[0].client == message->client &&
                itr->ID == transferId) {
                transfer = itr;
                break;
            }
        }
    }

    if (transfer != NULL && transfer->Dma.Context != NULL) {
        // Endpoint reset completes asynchronously. Queue through the usual HCI
        // state machine instead of touching an old descriptor chain.
        if (transfer->State != USBTRANSFER_STATE_COMPLETED) {
            status = OS_EBUSY;
        } else {
            status = USBTransferDmaPrepare(transfer);
            if (status == OS_EOK) {
                status = HCITransferQueue(transfer);
            }
        }
    } else if (transfer != NULL) {
        UsbManagerChainEnumerate(
                controller,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_RESET,
                HCIProcessElement,
                transfer
        );
        HCIProcessEvent(controller, HCIPROCESS_EVENT_RESET_DONE, transfer);
        __Transfer_Reset(transfer);
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
    if (controller) {
        foreach(node, &controller->TransactionList) {
            UsbManagerTransfer_t* itr = (UsbManagerTransfer_t*)node->value;
            if (itr->DeferredMessage[0].client == message->client &&
                itr->ID == transferId) {
                transfer = itr;
                break;
            }
        }
    }

    // Dequeue and send result back
    if (transfer != NULL) {
        status = HCITransferDequeue(transfer);
    }
    
    ctt_usbhost_dequeue_response(message, status);
}
