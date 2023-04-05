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
#include <ddk/utils.h>
#include <os/handle.h>
#include <os/shm.h>
#include "hci.h"
#include <stdlib.h>
#include "transfer.h"

#include "ctt_driver_service_server.h"
#include "ctt_usbhost_service_server.h"

extern gracht_server_t* __crt_get_module_server(void);

static unsigned int
__ConvertFlags(
        _In_ unsigned int flags)
{
    if (flags & USB_TRANSFER_NO_NOTIFICATION) {

    }
    if (flags & USB_TRANSFER_SHORT_NOT_OK) {

    }
}

static oserr_t
__AttachSGTables(
        _In_ UsbManagerTransfer_t* usbTransfer,
        _In_ USBTransfer_t*        transfer,
        _In_ SHMSGTable_t          sgTables[USB_TRANSACTIONCOUNT])
{
    // When attaching to dma buffers make sure we don't attach
    // multiple times as we can then save a system call or two
    for (int i = 0; i < transfer->TransactionCount; i++) {
        if (transfer->Transactions[i].BufferHandle == UUID_INVALID) {
            continue;
        }

        if (i != 0 && transfer->Transactions[i].BufferHandle ==
                      transfer->Transactions[i - 1].BufferHandle) {
            memcpy(&sgTables[i], &sgTables[i - 1], sizeof(SHMSGTable_t));
            memcpy(&usbTransfer->SHMHandles[i], &usbTransfer->SHMHandles[i - 1], sizeof(OSHandle_t));
        } else if (i == 2 && transfer->Transactions[i].BufferHandle ==
                             transfer->Transactions[i - 2].BufferHandle) {
            memcpy(&sgTables[i], &sgTables[i - 2], sizeof(SHMSGTable_t));
            memcpy(&usbTransfer->SHMHandles[i], &usbTransfer->SHMHandles[i - 2], sizeof(OSHandle_t));
        } else {
            oserr_t oserr = SHMAttach(
                    transfer->Transactions[i].BufferHandle,
                    &usbTransfer->SHMHandles[i]
            );
            if (oserr != OS_EOK) {
                return oserr;
            }

            oserr = SHMGetSGTable(
                    &usbTransfer->SHMHandles[i],
                    &sgTables[i],
                    -1
            );
            if (oserr != OS_EOK) {
                return oserr;
            }
        }
    }
    return OS_EOK;
}

static void
__FreeSGTables(
        _In_ SHMSGTable_t sgTables[USB_TRANSACTIONCOUNT])
{
    // Keep in mind that they may be copies of each other
    for (int i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        if (sgTables[i].Entries == NULL) {
            continue;
        }

        if (i > 0 && sgTables[i].Entries == sgTables[i - 1].Entries) {
            // do nothing, we already freed
        } else if (i == 2 && sgTables[i].Entries == sgTables[i - 2].Entries) {
            // do nothing, we already freed
        } else {
            free(sgTables[i].Entries);
        }
    }
}

UsbManagerTransfer_t*
UsbManagerCreateTransfer(
        _In_ struct gracht_message* message,
        _In_ USBTransfer_t*         transfer,
        _In_ uuid_t                 transferId,
        _In_ uuid_t                 deviceId)
{
    UsbManagerController_t* controller;
    UsbManagerTransfer_t*   usbTransfer;
    SHMSGTable_t            sgTables[USB_TRANSACTIONCOUNT];
    oserr_t                 oserr;
    
    TRACE("UsbManagerCreateTransfer(transfer=0x%" PRIxIN ", message=0x%" PRIxIN ", deviceId=%u)",
        transfer, message, deviceId);

    controller = (UsbManagerController_t*)UsbManagerGetController(deviceId);
    if (!controller) {
        return NULL;
    }

    usbTransfer = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t) + GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (!usbTransfer) {
        return NULL;
    }
    
    memset(usbTransfer, 0, sizeof(UsbManagerTransfer_t));
    gracht_server_defer_message(message, &usbTransfer->DeferredMessage[0]);

    ELEMENT_INIT(&usbTransfer->ListHeader, 0, usbTransfer);
    usbTransfer->ID = transferId;
    usbTransfer->DeviceID = deviceId;
    usbTransfer->Type = transfer->Type;
    memcpy(&usbTransfer->Address, &transfer->Address, sizeof(USBAddress_t));
    usbTransfer->MaxPacketSize = transfer->MaxPacketSize;
    usbTransfer->State = USBTRANSFER_STATE_CREATED;
    usbTransfer->Status = TransferOK;
    usbTransfer->Flags = __ConvertFlags(transfer->Flags);

    // Get the SG tables before calculating TDs
    oserr = __AttachSGTables(usbTransfer, transfer, sgTables);
    if (oserr != OS_EOK) {
        UsbManagerDestroyTransfer(usbTransfer);
        return NULL;
    }

    // Count the needed number of transfer elements
    usbTransfer->ElementCount = HCITransferElementsNeeded(transfer->Transactions, sgTables);
    if (!usbTransfer->ElementCount) {
        UsbManagerDestroyTransfer(usbTransfer);
        return NULL;
    }

    // Allocate the transfer element array
    usbTransfer->Elements = calloc(sizeof(struct TransferElement), usbTransfer->ElementCount);
    if (usbTransfer->Elements == NULL) {
        UsbManagerDestroyTransfer(usbTransfer);
        return NULL;
    }
    HCITransferElementFill(transfer->Transactions, sgTables, usbTransfer->Elements);
    __FreeSGTables(sgTables);
    list_append(&controller->TransactionList, &usbTransfer->ListHeader);
    return usbTransfer;
}

static void
__ReleaseSHMBuffers(
        _In_ OSHandle_t handles[USB_TRANSACTIONCOUNT])
{
    // Keep in mind that they may be copies of each other
    for (int i = 0; i < USB_TRANSACTIONCOUNT; i++) {
        if (handles[i].ID == UUID_INVALID) {
            continue;
        }

        if (i > 0 && handles[i].ID == handles[i - 1].ID) {
            // do nothing, we already freed
        } else if (i == 2 && handles[i].ID == handles[i - 2].ID) {
            // do nothing, we already freed
        } else {
            OSHandleDestroy(&handles[i]);
        }
    }
}

void
UsbManagerDestroyTransfer(
    _In_ UsbManagerTransfer_t* transfer)
{
    UsbManagerController_t* controller;

    if (!transfer) {
        return;
    }

    controller = (UsbManagerController_t*)UsbManagerGetController(transfer->DeviceID);
    if (controller) {
        list_remove(&controller->TransactionList, &transfer->ListHeader);
    }
    __ReleaseSHMBuffers(transfer->SHMHandles);
    free(transfer->Elements);
    free(transfer);
}

void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t* transfer)
{
    size_t bytesTransferred;
    
    TRACE("UsbManagerSendNotification(transfer=0x%" PRIxIN ")", transfer);
    
    // If user doesn't want, ignore
    TRACE("UsbManagerSendNotification transfer type=%u", transfer->Base.Type);
    if (transfer->Flags & __USBTRANSFER_FLAG_SILENT) {
        return;
    }

    // If notification has been sent on control/bulk do not send again
    if (__Transfer_IsAsync(transfer)) {
        if ((transfer->Flags & __USBTRANSFER_FLAG_NOTIFIED)) {
            return;
        }
        transfer->Flags |= __USBTRANSFER_FLAG_NOTIFIED;
        bytesTransferred = transfer->TData.Async.BytesTransferred;

        TRACE("UsbManagerSendNotification is notifiyng");
        ctt_usbhost_queue_response(&transfer->DeferredMessage[0], transfer->Status, bytesTransferred);
    } else if (transfer->Type == USBTRANSFER_TYPE_INTERRUPT) {
        ctt_usbhost_event_transfer_status_single(
                __crt_get_module_server(),
                transfer->DeferredMessage[0].client,
                transfer->ID,
                transfer->Status,
                transfer->TData.Periodic.CurrentDataIndex
        );
        if (transfer->Status == TransferOK) {
            transfer->TData.Periodic.CurrentDataIndex = ADDLIMIT(0, transfer->TData.Periodic.CurrentDataIndex,
                                                  transfer->Elements[0].Length,
                                                  transfer->TData.Periodic.PeriodicBufferSize);
        }
    }
}

void ctt_usbhost_queue_invocation(struct gracht_message* message, const uuid_t processId, const uuid_t deviceId,
                                  const uuid_t transferId, const uint8_t* transfer, const uint32_t transfer_count)
{
    UsbManagerTransfer_t* usbTransfer;
    oserr_t               oserr;

    usbTransfer = UsbManagerCreateTransfer(
            message,
            (USBTransfer_t*)transfer,
            transferId,
            deviceId
    );
    if (usbTransfer == NULL) {
        ctt_usbhost_queue_response(message, TransferInvalid, 0);
        return;
    }

    oserr = HciQueueTransferGeneric(usbTransfer);
    if (oserr != OS_EOK) {
        UsbManagerDestroyTransfer(usbTransfer);
        ctt_usbhost_queue_response(message, TransferInvalid, 0);
    }
}

void ctt_usbhost_queue_periodic_invocation(struct gracht_message* message, const uuid_t processId,
                                           const uuid_t deviceId, const uuid_t transferId, const uint8_t* transfer, const uint32_t transfer_count)
{
    UsbManagerTransfer_t* usbTransfer;
    oserr_t               oserr;

    usbTransfer = UsbManagerCreateTransfer(
            message,
            (USBTransfer_t*)transfer,
            transferId,
            deviceId
    );
    if (usbTransfer == NULL) {
        ctt_usbhost_queue_periodic_response(message, TransferInvalid);
        return;
    }

    if (usbTransfer->Type == USBTRANSFER_TYPE_ISOC) {
        oserr = HciQueueTransferIsochronous(usbTransfer);
    } else {
        oserr = HciQueueTransferGeneric(usbTransfer);
    }

    if (oserr != OS_EOK) {
        UsbManagerDestroyTransfer(usbTransfer);
        ctt_usbhost_queue_periodic_response(message, TransferInvalid);
        return;
    }
    ctt_usbhost_queue_periodic_response(message, TransferOK);
}

void ctt_usbhost_reset_periodic_invocation(struct gracht_message* message, const uuid_t processId,
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

    if (transfer != NULL) {
        UsbManagerChainEnumerate(
                controller,
                transfer->EndpointDescriptor,
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
        status = HciDequeueTransfer(transfer);
    }
    
    ctt_usbhost_dequeue_response(message, status);
}
