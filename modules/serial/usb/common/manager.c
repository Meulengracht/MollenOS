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
 *
 *
 * USB Controller Manager
 * - Shared controller management for the USB drivers.
 */

//#define __TRACE

#include <assert.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <ddk/barrier.h>
#include <ds/hashtable.h>
#include <event.h>
#include "hci.h"
#include <ioset.h>
#include <io.h>
#include <ioctl.h>
#include "manager.h"
#include <stdlib.h>

#include "ctt_driver_service_server.h"
#include "ctt_usbhost_service_server.h"

extern gracht_server_t* __crt_get_module_server(void);

struct usb_controller_device_index {
    uuid_t                  deviceId;
    UsbManagerController_t* pointer;
};

struct USBManagerEndpointToggle {
    uuid_t Address;
    int    Toggle;
};

#define ITERATOR_CONTINUE           0
#define ITERATOR_STOP               (1 << 0)
#define ITERATOR_REMOVE             (1 << 1)

static void __ControllerMonitor(void*,void*);

static uint64_t default_dev_hash(const void*);
static int      default_dev_cmp(const void*, const void*);

static uint64_t endpoint_hash(const void*);
static int      endpoint_cmp(const void*, const void*);

static int         g_hciCheckupRegistered = 0;
static hashtable_t g_controllers;
static list_t      g_destroyPending = LIST_INIT;

oserr_t
UsbManagerInitialize(void)
{
    // Wait up to 5 seconds for the USB service before starting the manager.
    if (WaitForUsbService(5000) != OS_EOK) {
        ERROR(" => Failed to start usb manager, as usb service never became available.");
        return OS_ETIMEOUT;
    }

    hashtable_construct(
            &g_controllers,
            0,
            sizeof(struct usb_controller_device_index),
                    default_dev_hash,
                    default_dev_cmp
    );
    return OS_EOK;
}

void
UsbManagerDestroy(void)
{
    hashtable_destroy(&g_controllers);
}

UsbManagerController_t*
UsbManagerCreateController(
    _In_ Device_t*           device,
    _In_ enum USBControllerKind kind,
    _In_ size_t                 structureSize)
{
    UsbManagerController_t* controller;
    int                     opt = 1;

    // Queue the monitor before we add a controller to the registry.
    if (!g_hciCheckupRegistered) {
        if (usched_job_queue(__ControllerMonitor, NULL) == UUID_INVALID) {
            return NULL;
        }
        g_hciCheckupRegistered = 1;
    }

    controller = (UsbManagerController_t*)malloc(structureSize);
    if (!controller) {
        return NULL;
    }
    memset(controller, 0, structureSize);

    controller->Device = device;
    controller->Kind = kind;
    list_construct(&controller->TransactionList);
    list_construct(&controller->ReadyTransfers);
    list_construct(&controller->WaitingTransfers);
    hashtable_construct(
            &controller->Endpoints,
            0,
            sizeof(struct USBManagerEndpointToggle),
                    endpoint_hash,
                    endpoint_cmp
    );
    spinlock_init(&controller->Lock);

    // Use an event descriptor to receive interrupt notifications.
    controller->event_descriptor = eventd(0, EVT_RESET_EVENT);
    if (controller->event_descriptor < 0) {
        free(controller);
        return NULL;
    }

    // Add the descriptor to the gracht server and make reads non-blocking.
    ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
               IOSET_ADD, controller->event_descriptor,
               &(struct ioset_event){ .data.context = controller, .events = IOSETSYN });
    ioctl(controller->event_descriptor, FIONBIO, &opt);

    // add indexes
    hashtable_set(&g_controllers, &(struct usb_controller_device_index) {
        .deviceId = device->Id, .pointer = controller });

    return controller;
}

oserr_t
UsbManagerRegisterController(
    _In_ UsbManagerController_t* controller)
{
    oserr_t oserr = UsbControllerRegister(
            controller->Device,
            controller->Kind,
            (int)controller->PortCount
    );
    if (oserr != OS_EOK) {
        ERROR("[UsbManagerRegisterController] failed with code %u", oserr);
    } else {
        controller->State.Registered = true;
    }
    return oserr;
}

bool
UsbManagerCompleteControllerInitialization(
    _In_ UsbManagerController_t* controller)
{
    // If a fatal interrupt requested destruction while setup waited for hardware,
    // leave the controller in that state. It must stay on the cleanup queue and
    // reject new requests even though initialization succeeded.
    if (controller->State.Phase != USB_CONTROLLER_INITIALIZING) {
        return false;
    }
    controller->State.Phase = USB_CONTROLLER_READY;
    return true;
}

void
UsbManagerDestroyController(
    _In_ UsbManagerController_t* controller)
{
    if (controller->State.Phase >= USB_CONTROLLER_DESTROY_PENDING) {
        list_remove(&g_destroyPending, &controller->DestroyElement);
    }

    // clear the lists
    UsbManagerClearTransfers(controller);

    // remove the controller indexes
    hashtable_remove(
        &g_controllers,
        &(struct usb_controller_device_index) {
            .deviceId = controller->Device->Id
        }
    );

    // clean up resources
    hashtable_destroy(&controller->Endpoints);

    // Remove the event descriptor from the gracht server.
    ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
               IOSET_DEL, controller->event_descriptor, NULL);
    close(controller->event_descriptor);

    // We keep track of whether the controller ever
    // registered with usbd
    if (controller->State.Registered) {
        controller->State.Registered = false;
        UsbControllerUnregister(controller->Device->Id);
    }
}

void
UsbManagerDeferControllerDestroy(
    _In_ UsbManagerController_t* controller)
{
    // Only controller cleanup uses this queue. Keep the same controller between
    // attempts so its cleared fields show which resources have been released.
    if (controller->State.Phase < USB_CONTROLLER_DESTROY_PENDING) {
        ELEMENT_INIT(&controller->DestroyElement, 0, controller);
        list_append(&g_destroyPending, &controller->DestroyElement);
        controller->State.Phase = USB_CONTROLLER_DESTROY_PENDING;
    }
}

bool
UsbManagerBeginControllerDestroy(
    _In_ UsbManagerController_t* controller)
{
    // Stop new requests even if an operation is still running. If destruction
    // was already requested, keep its queue entry and let that attempt finish.
    UsbManagerDeferControllerDestroy(controller);
    if (controller->State.Phase == USB_CONTROLLER_DESTROYING ||
        controller->State.OperationsInFlight != 0) {
        return false;
    }

    controller->State.Phase = USB_CONTROLLER_DESTROYING;
    return true;
}

void
UsbManagerRetryControllerDestroy(
    _In_ UsbManagerController_t* controller)
{
    // The current cleanup attempt must call this before returning for a retry.
    // Keep new requests blocked and the queue entry linked until cleanup finishes.
    controller->State.Phase = USB_CONTROLLER_DESTROY_PENDING;
}

static void
__RetryControllerDestruction(void)
{
    element_t* node;
    size_t     remaining;

    // Destruction can yield during IPC, allowing another request to remove a
    // queued controller. Move each entry to the tail before the call and read
    // the head again afterwards. A saved next pointer may no longer be valid.
    // Run this outside the registry callback and try each pending entry once.
    remaining = g_destroyPending.count;
    while (remaining-- != 0 && g_destroyPending.head != NULL) {
        node = g_destroyPending.head;
        list_remove(&g_destroyPending, node);
        list_append(&g_destroyPending, node);
        HCIControllerDestroy(node->value);
    }
}

static void
__CheckController(
    _In_ int         index,
    _In_ const void* element,
    _In_ void*       context)
{
    const struct usb_controller_device_index* deviceIndex = element;
    UsbManagerController_t*                   controller = deviceIndex->pointer;
    _CRT_UNUSED(index);
    _CRT_UNUSED(context);

    if (controller->State.Phase == USB_CONTROLLER_READY) {
        controller->State.OperationsInFlight++;
        HciTimerCallback(controller);
        controller->State.OperationsInFlight--;
    }
}

static void
__ControllerMonitor(
    _In_ void* argument,
    _In_ void* cancellationToken)
{
    struct timespec wakeUp;
    _CRT_UNUSED(argument);

    timespec_get(&wakeUp, TIME_UTC);
    while (usched_is_cancelled(cancellationToken) == false) {
        wakeUp.tv_sec += 1;
        usched_job_sleep(&wakeUp);
        hashtable_enumerate(
                &g_controllers,
                __CheckController,
                NULL
        );
        __RetryControllerDestruction();
    }
}

void
UsbManagerIterateTransfers(
    _In_ UsbManagerController_t* controller,
    _In_ UsbTransferItemCallback itemCallback,
    _In_ void*                   context)
{
    foreach_nolink(node, &controller->TransactionList) {
        UsbManagerTransfer_t* transfer = (UsbManagerTransfer_t*)node->value;
        int                   status = itemCallback(controller, transfer, context);
        if (status & ITERATOR_REMOVE) {
            element_t* next = node->next;
            if (USBTransferDestroy(transfer) != OS_EOK) {
                // Finalization may already have released the descriptor chain.
                // Retry destruction without scanning that chain again.
                transfer->State = USBTRANSFER_STATE_CLEANUP;
            }
            node = next;
        }
        else {
            node = node->next;
        }

        if (status & ITERATOR_STOP) {
            break;
        }
    }
}

UsbManagerController_t*
UsbManagerGetController(
    _In_ uuid_t deviceId)
{
    struct usb_controller_device_index* index = hashtable_get(&g_controllers,
            &(struct usb_controller_device_index) { .deviceId = deviceId });
    if (!index) {
        return NULL;
    }
    return index->pointer;
}

int
UsbManagerGetToggle(
    _In_ UsbManagerController_t* controller,
    _In_ USBAddress_t*           address)
{
    struct USBManagerEndpointToggle* endpoint = hashtable_get(
            &controller->Endpoints,
            &(struct USBManagerEndpointToggle) {
                    .Address = (((uint32_t)address->DeviceAddress << 8) | address->EndpointAddress)
            }
    );
    if (endpoint != NULL) {
        return endpoint->Toggle;
    }
    return 0;
}

void
UsbManagerSetToggle(
    _In_ UsbManagerController_t* controller,
    _In_ USBAddress_t*           address,
    _In_ int                     toggle)
{
    hashtable_set(
            &controller->Endpoints,
            &(struct USBManagerEndpointToggle) {
                    .Address = (((uint32_t)address->DeviceAddress << 8) | address->EndpointAddress),
                    .Toggle = toggle
            }
    );
}

void ctt_usbhost_reset_endpoint_invocation(
    struct gracht_message* message,
    const uuid_t           deviceId,
    const uint8_t          hub,
    const uint8_t          port,
    const uint8_t          device,
    const uint8_t          endpoint)
{
    USBAddress_t            address = { hub, port, device, endpoint };
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    oserr_t                 err;
    if (controller == NULL) {
        ctt_usbhost_reset_endpoint_response(message, OS_ENOENT);
        return;
    }
    // The HCI resets the toggle and handles any halt or dequeue recovery.
    err = HCIEndpointReset(controller, &address);
    ctt_usbhost_reset_endpoint_response(message, err);
}

void ctt_usbhost_configure_hub_invocation(
    struct gracht_message* message,
    const uuid_t           deviceId,
    const uint8_t          hubAddress,
    const uint8_t          portCount,
    const uint16_t         characteristics)
{
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    oserr_t                 err;
    
    if (controller == NULL) {
        ctt_usbhost_configure_hub_response(message, OS_ENOENT);
        return;
    }

    err = HCIConfigureHub(controller, hubAddress, portCount, characteristics);
    ctt_usbhost_configure_hub_response(message, err);
}

void ctt_usbhost_device_detach_invocation(
    struct gracht_message* message,
    const uuid_t           controllerDeviceId,
    const uint8_t          hubAddress,
    const uint8_t          portAddress,
    const uint8_t          deviceAddress)
{
    USBAddress_t            address = { hubAddress, portAddress, deviceAddress, 0 };
    UsbManagerController_t* controller = UsbManagerGetController(controllerDeviceId);
    oserr_t                 err;

    if (controller == NULL) {
        ctt_usbhost_device_detach_response(message, OS_ENOENT);
        return;
    }

    err = HCIDeviceDetach(controller, &address);
    ctt_usbhost_device_detach_response(message, err);
}

static void
__QueueWaitingTransfers(
    _In_ UsbManagerController_t* controller)
{
    foreach(node, &controller->TransactionList) {
        UsbManagerTransfer_t* transfer = (UsbManagerTransfer_t*)node->value;

        if (!transfer->Submitting && transfer->State == USBTRANSFER_STATE_WAITING) {
            if (transfer->Type == USBTRANSFER_TYPE_ISOC) {
                HCITransferQueueIsochronous(transfer);
            } else {
                HCITransferQueue(transfer);
            }
            break;
        }
    }
}

static oserr_t
__FinalizeTransfer(
    _In_ UsbManagerTransfer_t* transfer)
{
    bool isPartial = false;
    TRACE("__FinalizeTransfer()");

    // Control and bulk transfers may need another group of descriptors.
    // Periodic transfers reach here only after cancellation or detach, so they
    // must not be queued again.
    if (__Transfer_IsAsync(transfer)) {
        isPartial = __Transfer_IsPartial(transfer);
    }

    // If only part of the transfer has completed, queue the remaining elements
    // before notifying the caller.
    if (isPartial && transfer->ResultCode == USBTRANSFERCODE_SUCCESS) {
        HCITransferQueue(transfer);
        return OS_EINCOMPLETE;
    }

    USBTransferNotify(transfer);
    return OS_EOK;
}

void
UsbManagerClearTransfers(
    _In_ UsbManagerController_t* controller)
{
    UsbManagerTransfer_t* transfer;

    while (controller->TransactionList.head != NULL) {
        transfer = controller->TransactionList.head->value;
        if (transfer->Submitting) {
            return;
        }

        transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        
        if (HCITransferFinalize(controller, transfer, false) != OS_EOK) {
            return;
        }

        if (USBTransferDmaComplete(transfer, transfer->Dma.Completed) != OS_EOK) {
            return;
        }
        
        USBTransferNotify(transfer);
        if (USBTransferDestroy(transfer) != OS_EOK) {
            return;
        }
    }
}

static int
__ScheduleTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ void*                   context)
{
    _CRT_UNUSED(context);

    if (transfer->Submitting) {
        return ITERATOR_CONTINUE;
    }

    if (transfer->State == USBTRANSFER_STATE_UNSCHEDULE) {
        TRACE("__ScheduleTransfer: [unschedule] transferID=%u", transfer->ID);
        if (HCITransferFinalize(controller, transfer, true) != OS_EOK) {
            return ITERATOR_CONTINUE;
        }

        transfer->State = USBTRANSFER_STATE_CLEANUP;
    } else if (transfer->State == USBTRANSFER_STATE_SCHEDULE) {
        TRACE("__ScheduleTransfer: [schedule] transferID=%u", transfer->ID);
        UsbManagerChainEnumerate(
                controller,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_LINK,
                HCIProcessElement,
                transfer
        );
        transfer->Dma.Linked = true;
        transfer->State = USBTRANSFER_STATE_QUEUED;
    }
    return ITERATOR_CONTINUE;
}

void
UsbManagerScheduleTransfers(
    _In_ UsbManagerController_t* controller)
{
    UsbManagerIterateTransfers(
            controller,
            __ScheduleTransfer,
            NULL
    );
}

static int
__FinishLegacyTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t* transfer);

static int
__ProcessCleanup(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer)
{
    if (controller->Kind != USBCONTROLLER_KIND_XHCI) {
        return __FinishLegacyTransfer(controller, transfer);
    }

    // Let the HCI release descriptors kept between periodic submissions.
    if (HCITransferFinalize(controller, transfer, false) != OS_EOK) {
        return ITERATOR_CONTINUE;
    }

    // Clear RootElement after freeing the chain so the next submission allocates
    // a new descriptor.
    if (transfer->RootElement != NULL) {
        UsbManagerChainEnumerate(
                controller,
                transfer->RootElement,
                USB_CHAIN_DEPTH,
                HCIPROCESS_REASON_CLEANUP,
                HCIProcessElement,
                transfer
        );
        transfer->RootElement = NULL;
    }

    // A partially completed transfer may have been queued again. Only remove
    // it when finalization returns OS_EOK.
    if (__FinalizeTransfer(transfer) == OS_EOK) {
        return ITERATOR_REMOVE;
    }
    transfer->State = USBTRANSFER_STATE_COMPLETED;
    return ITERATOR_CONTINUE;
}

static bool
__TransferCompleted(
    _In_ UsbManagerTransfer_t*               transfer,
    _In_ struct HCIProcessReasonScanContext* scanContext)
{
    TRACE("__TransferCompleted(transfer=%u, result=%u)", transfer->ID, scanContext->Result);

    // An error ends the transfer.
    if (scanContext->Result != USBTRANSFERCODE_SUCCESS) {
        return true;
    }

    // A short packet also ends the transfer.
    TRACE("__TransferCompleted: short=%u", scanContext->Short);
    if (scanContext->Short) {
        return true;
    }

    // Otherwise, all elements must have executed.
    TRACE("__TransferCompleted: executed=%u/%u", scanContext->ElementsExecuted, transfer->ChainLength);
    if (scanContext->ElementsExecuted == transfer->ChainLength) {
        return true;
    }
    return false;
}

static void
__ResetTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer)
{
    UsbManagerChainEnumerate(
            controller,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_RESET,
            HCIProcessElement,
            transfer
    );
    HCIProcessEvent(controller, HCIPROCESS_EVENT_RESET_DONE, transfer);
}

static int
__RestartLegacyTransfer(
    _In_ UsbManagerTransfer_t* transfer)
{
    oserr_t status;

    status = USBTransferDmaPrepare(transfer);
    if (transfer->State == USBTRANSFER_STATE_CLEANUP) {
        return ITERATOR_CONTINUE;
    }

    if (status == OS_EOK) {
        status = transfer->Type == USBTRANSFER_TYPE_ISOC ?
            HCITransferQueueIsochronous(transfer) : HCITransferQueue(transfer);
    }

    if (status != OS_EOK) {
        // Keep the inactive descriptors and mapping for another attempt.
        // The caller was already notified, so retry without sending it again.
        if (transfer->State != USBTRANSFER_STATE_CLEANUP) {
            transfer->State = USBTRANSFER_STATE_RESTART;
        }
        return ITERATOR_CONTINUE;
    }

    transfer->Flags &= ~__USBTRANSFER_FLAG_NOTIFIED;
    return ITERATOR_CONTINUE;
}

static int
__FinishLegacyTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer)
{
    bool    periodic = __Transfer_IsPeriodic(transfer);
    bool    success = transfer->ResultCode == USBTRANSFERCODE_SUCCESS;
    oserr_t status;

    if (!periodic && success && !__Transfer_IsPartial(transfer) &&
        !(transfer->Flags & __USBTRANSFER_FLAG_SHORT)) {
        status = USBTransferDmaComplete(transfer, transfer->Dma.Completed);
        if (status != OS_EOK) {
            return ITERATOR_CONTINUE;
        }
        USBTransferNotify(transfer);
    }

    // Successful periodic packets leave inactive descriptors linked for reuse.
    // Async chains and cancellation must pass the HCI's unlink/stop checks first.
    if (!periodic || !success ||
        ((transfer->Flags & __USBTRANSFER_FLAG_SHORT) && transfer->ChainLength > 1)) {
        status = HCITransferFinalize(controller, transfer, false);
        if (status != OS_EOK) {
            return ITERATOR_CONTINUE;
        }
    }

    if (!periodic && success && __Transfer_IsPartial(transfer)) {
        // The buffer stays mapped for the device while we allocate the next
        // group of descriptors. There is no need to map or sync it again here.
        status = HCITransferQueue(transfer);
        if (status != OS_EOK) {
            transfer->ResultCode = USBTRANSFERCODE_INVALID;
            transfer->State = USBTRANSFER_STATE_FINISHING;
        }
        return ITERATOR_CONTINUE;
    }

    status = USBTransferDmaComplete(transfer, transfer->Dma.Completed);
    if (status != OS_EOK) {
        return ITERATOR_CONTINUE;
    }

    if (!(transfer->Flags & __USBTRANSFER_FLAG_NOTIFIED)) {
        if (transfer->ResultCode != USBTRANSFERCODE_NAK) {
            USBTransferNotify(transfer);
        }
        transfer->Flags |= __USBTRANSFER_FLAG_NOTIFIED;
    }

    if (!periodic) {
        return ITERATOR_REMOVE;
    }
    
    // If notification yielded to cancellation or controller removal, leave the
    // transfer for cleanup. It must be unlinked before another packet is queued.
    if (transfer->ResultCode == USBTRANSFERCODE_CANCELLED ||
        controller->State.Phase >= USB_CONTROLLER_DESTROY_PENDING) {
        transfer->ResultCode = USBTRANSFERCODE_CANCELLED;
        transfer->State = USBTRANSFER_STATE_CLEANUP;
        return transfer->RootElement == NULL ? ITERATOR_REMOVE : ITERATOR_CONTINUE;
    }
    
    if (transfer->ResultCode == USBTRANSFERCODE_STALL) {
        transfer->State = USBTRANSFER_STATE_STALLED;
        return ITERATOR_CONTINUE;
    }
    
    transfer->State = USBTRANSFER_STATE_RESTART;
    return __RestartLegacyTransfer(transfer);
}

static int
__CheckLegacyTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer)
{
    struct HCIProcessReasonScanContext context = {
        .Transfer = transfer,
        .Result = USBTRANSFERCODE_SUCCESS
    };

    dma_rmb();
    UsbManagerChainEnumerate(
        controller,
        transfer->RootElement,
        USB_CHAIN_DEPTH,
        HCIPROCESS_REASON_SCAN,
        HCIProcessElement,
        &context
    );
    
    // Each descriptor is counted once. Save the completed byte count before
    // calling DMA completion, so a failed call does not lose it on the next scan.
    transfer->Dma.Completed += context.BytesTransferred;
    
    if (__Transfer_IsAsync(transfer)) {
        transfer->TData.Async.ElementsCompleted += context.ElementsProcessed;
        transfer->TData.Async.BytesTransferred += context.BytesTransferred;
    }
    
    if (!__TransferCompleted(transfer, &context)) {
        return ITERATOR_CONTINUE;
    }
    
    transfer->ResultCode = context.Result;
    if (context.Short) {
        transfer->Flags |= __USBTRANSFER_FLAG_SHORT;
    }

    if ((context.Result != USBTRANSFERCODE_SUCCESS || context.Short) && context.ElementsProcessed != 0) {
        UsbManagerSetToggle(controller, &transfer->Address, context.LastToggle);
    }
    
    transfer->State = USBTRANSFER_STATE_FINISHING;
    return __FinishLegacyTransfer(controller, transfer);
}

static int
__CheckTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer)
{
    struct HCIProcessReasonScanContext context = {
            .Transfer = transfer,
            .Result = USBTRANSFERCODE_SUCCESS
    };
    
    if (controller->Kind != USBCONTROLLER_KIND_XHCI) {
        return __CheckLegacyTransfer(controller, transfer);
    }

    if (transfer->Dma.HardwareActive) {
        return ITERATOR_CONTINUE;
    }

    TRACE("__ProcessTransfer: starting scan id=%u, status=%u", transfer->ID, context.Result);
    UsbManagerChainEnumerate(
            controller,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_SCAN,
            HCIProcessElement,
            &context
    );
    TRACE("__ProcessTransfer: finished scan status=%u", context.Result);

    if (__TransferCompleted(transfer, &context)) {
        if (USBTransferDmaComplete(transfer, context.BytesTransferred) != OS_EOK) {
            return ITERATOR_CONTINUE;
        }
    }

    // Count completed elements and bytes unless a toggle mismatch requires a retry.
    if (__Transfer_IsAsync(transfer) && context.Result != USBTRANSFERCODE_DATATOGGLEMISMATCH) {
        transfer->TData.Async.ElementsCompleted += context.ElementsProcessed;
        transfer->TData.Async.BytesTransferred += context.BytesTransferred;
    }

    if (!__TransferCompleted(transfer, &context)) {
        return ITERATOR_CONTINUE;
    }

    // In the case of a data toggle mismatch, we reset the transfer and try again
    // with updated toggles.
    if (context.Result == USBTRANSFERCODE_DATATOGGLEMISMATCH) {
        __ResetTransfer(controller, transfer);
        __Transfer_Reset(transfer);
        return ITERATOR_CONTINUE;
    }

    // Save the result for completion checks and notification.
    transfer->ResultCode = context.Result;
    if (context.Short) {
        transfer->Flags |= __USBTRANSFER_FLAG_SHORT;
    }

    // If an error or short packet ended the transfer after some elements ran,
    // update the stored toggle to match the last processed element.
    if ((context.Result != USBTRANSFERCODE_SUCCESS || context.Short) &&
        context.ElementsProcessed > 0) {
         UsbManagerSetToggle(controller, &transfer->Address, context.LastToggle);
    }

    if (__Transfer_IsPeriodic(transfer)) {
        if (context.Result != USBTRANSFERCODE_NAK) {
            USBTransferNotify(transfer);
        }

        if (HCITransferFinalize(controller, transfer, false) != OS_EOK) {
            return ITERATOR_CONTINUE;
        }
        
        if (context.Result == USBTRANSFERCODE_STALL) {
            transfer->State = USBTRANSFER_STATE_STALLED;
        } else {
            __Transfer_Reset(transfer);
            if (USBTransferDmaPrepare(transfer) == OS_EOK && HCITransferQueue(transfer) == OS_EOK) {
                return ITERATOR_CONTINUE;
            }
            
            transfer->State = USBTRANSFER_STATE_COMPLETED;
            transfer->ResultCode = USBTRANSFERCODE_BUFFERERROR;
            USBTransferNotify(transfer);
        }
        return ITERATOR_CONTINUE;
    }

    if (HCITransferFinalize(controller, transfer, false) != OS_EOK) {
        return ITERATOR_CONTINUE;
    }

    transfer->RootElement = NULL;
    if (__FinalizeTransfer(transfer) == OS_EOK) {
        return ITERATOR_REMOVE;
    }
    return ITERATOR_CONTINUE;
}

static int
__ProcessTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ void*                   context)
{
    TRACE("__ProcessTransfer(controller=0x%" PRIxIN ", transfer=0x%" PRIxIN ", flags=0x%x)",
          controller, transfer, transfer ? transfer->Flags : 0);
    _CRT_UNUSED(context);

    // Submission may yield while a cancellation request is handled. Wait for
    // submission to finish before freeing the mapping or descriptors.
    if (transfer->Submitting) {
        return ITERATOR_CONTINUE;
    }

    switch (transfer->State) {
        case USBTRANSFER_STATE_CREATED:
        case USBTRANSFER_STATE_WAITING:
        case USBTRANSFER_STATE_SCHEDULE:
        case USBTRANSFER_STATE_UNSCHEDULE:
        case USBTRANSFER_STATE_COMPLETED:
        case USBTRANSFER_STATE_STALLED:
            break;
        case USBTRANSFER_STATE_FINISHING:
            return __FinishLegacyTransfer(controller, transfer);
        case USBTRANSFER_STATE_RESTART:
            return __RestartLegacyTransfer(transfer);
        case USBTRANSFER_STATE_QUEUED:
            return __CheckTransfer(controller, transfer);
        case USBTRANSFER_STATE_CLEANUP:
            return __ProcessCleanup(controller, transfer);
    }
    return ITERATOR_CONTINUE;
}

static void
__ProcessReadyTransfers(
    _In_ UsbManagerController_t* controller)
{
    UsbManagerTransfer_t* transfer;
    int                   remaining = controller->ReadyTransfers.count;
    int                   result;
    oserr_t               status;

    // Try each queued transfer once per dispatch. A failed DMA sync or unmap
    // must wait until the next dispatch instead of spinning in this loop.
    while (remaining-- > 0 && controller->ReadyTransfers.head != NULL) {
        transfer = controller->ReadyTransfers.head->value;
        UsbManagerForgetTransferWork(transfer);
        
        if (transfer->Submitting) {
            UsbManagerQueueTransferWork(controller, transfer);
            continue;
        }

        if (transfer->State == USBTRANSFER_STATE_WAITING) {
            status = HCITransferQueue(transfer);
            if (status == OS_EOK) {
                continue;
            }

            transfer->ResultCode = USBTRANSFERCODE_INVALID;
            transfer->State = USBTRANSFER_STATE_CLEANUP;
        }
        
        result = __ProcessTransfer(controller, transfer, NULL);
        if (result & ITERATOR_REMOVE) {
            if (USBTransferDestroy(transfer) == OS_EOK) {
                continue;
            }
            transfer->State = USBTRANSFER_STATE_CLEANUP;
        }
        
        if (transfer->State == USBTRANSFER_STATE_CLEANUP ||
            (transfer->State == USBTRANSFER_STATE_QUEUED && !transfer->Dma.HardwareActive)) {
            UsbManagerQueueTransferWork(controller, transfer);
        }
    }
}

void
UsbManagerProcessTransfers(
    _In_ UsbManagerController_t* controller)
{
    if (controller->State.ProcessingTransfers ||
        controller->State.Phase >= USB_CONTROLLER_DESTROY_PENDING) {
        return;
    }
    // DMA syscalls and completion IPC can yield. Allow cancellation requests
    // during that time, but keep other dispatches from removing transfers while
    // this pass is using the list.
    controller->State.ProcessingTransfers = true;
    controller->State.OperationsInFlight++;
    
    if (controller->Kind == USBCONTROLLER_KIND_XHCI) {
        __ProcessReadyTransfers(controller);
    } else {
        UsbManagerIterateTransfers(controller, __ProcessTransfer, NULL);
        __QueueWaitingTransfers(controller);
    }
    
    controller->State.OperationsInFlight--;
    controller->State.ProcessingTransfers = false;
}


void
UsbManagerForgetTransferWork(
    _In_ UsbManagerTransfer_t* transfer)
{
    if (transfer->Work.Queue != NULL) {
        list_remove(transfer->Work.Queue, &transfer->Work.Header);
        transfer->Work.Queue = NULL;
    }
}

static void
__MoveTransferWork(
    _In_ list_t*              queue,
    _In_ UsbManagerTransfer_t* transfer)
{
    if (transfer->Work.Queue == queue) {
        return;
    }
    
    UsbManagerForgetTransferWork(transfer);
    ELEMENT_INIT(&transfer->Work.Header, 0, transfer);
    list_append(queue, &transfer->Work.Header);
    transfer->Work.Queue = queue;
}

void
UsbManagerQueueTransferWork(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer)
{
    __MoveTransferWork(&controller->ReadyTransfers, transfer);
}

void
UsbManagerWaitTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer)
{
    transfer->State = USBTRANSFER_STATE_WAITING;
    __MoveTransferWork(&controller->WaitingTransfers, transfer);
}

void
UsbManagerWakeTransfers(
    _In_ UsbManagerController_t* controller)
{
    UsbManagerTransfer_t* transfer;

    while (controller->WaitingTransfers.head != NULL) {
        transfer = controller->WaitingTransfers.head->value;
        UsbManagerQueueTransferWork(controller, transfer);
    }
}

void
UsbManagerChainEnumerate(
    _In_ UsbManagerController_t*     controller,
    _In_ uint8_t*                    elementRoot,
    _In_ int                         direction,
    _In_ int                         reason,
    _In_ UsbSchedulerElementCallback elementCallback,
    _In_ void*                       context)
{
    UsbSchedulerObject_t* object;
    UsbSchedulerPool_t*   pool;
    oserr_t               oserr;
    uint16_t              rootIndex;
    uint16_t              linkIndex;
    uint8_t*              element = elementRoot;
    assert(controller != NULL);
    assert(elementRoot != NULL);

    // Validate element and lookup pool
    oserr = UsbSchedulerGetPoolFromElement(
            controller->Scheduler,
            element,
            &pool
    );
    if (oserr != OS_EOK) {
        WARNING("UsbManagerChainEnumerate: cannot get object from pool");
        return;
    }

    object = USB_ELEMENT_OBJECT(pool, element);
    rootIndex = object->Index;
    linkIndex = (direction == USB_CHAIN_BREATH) ? object->BreathIndex : object->DepthIndex;
    while (element) {
        // Support null-elements
        if (controller->Scheduler->Settings.Flags & USB_SCHEDULER_NULL_ELEMENT) {
            if (linkIndex == USB_ELEMENT_NO_INDEX) {
                break;
            }
        }

        // The callback can stop enumeration by returning false.
        if (!elementCallback(controller, element, reason, context)) {
            break;
        }

        // Stop at the end of the chain or when we reach the root again.
        // Cyclic chains are valid, but each element must only be visited once.
        if (linkIndex == USB_ELEMENT_NO_INDEX || linkIndex == rootIndex) {
            break;
        }

        // Links can cross pools, so look up the pool for each element.
        pool = USB_ELEMENT_GET_POOL(controller->Scheduler, linkIndex);

        // Get the element and its scheduler object from that pool.
        element   = USB_ELEMENT_INDEX(pool, linkIndex);
        object    = USB_ELEMENT_OBJECT(pool, element);
        linkIndex = (direction == USB_CHAIN_BREATH) ? object->BreathIndex : object->DepthIndex;
    }
}

void
UsbManagerDumpChain(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ uint8_t*                ElementRoot,
    _In_ int                     Direction)
{
    UsbManagerChainEnumerate(
            Controller,
            ElementRoot,
            Direction,
            HCIPROCESS_REASON_DUMP,
            HCIProcessElement,
            Transfer
    );
}

static bool
__DumpScheduleElement(
    _In_ UsbManagerController_t* controller,
    _In_ uint8_t*                element,
    _In_ enum HCIProcessReason   reason,
    _In_ void*                   context)
{
    UsbManagerTransfer_t* Transfer = context;

    // set the fake transfer endpoint descriptor
    Transfer->RootElement = element;
    UsbManagerChainEnumerate(
            controller,
            element,
            USB_CHAIN_DEPTH,
            reason,
            HCIProcessElement,
            context
    );
    return true;
}

void
UsbManagerDumpSchedule(
    _In_ UsbManagerController_t* Controller)
{
    UsbManagerTransfer_t PseudoTransferObject = { { 0 } };
    for (int i = 0; i < Controller->Scheduler->Settings.FrameCount; i++) {
        WARNING("-------------------------- FRAME %i ---------------------------------", i);
        UsbManagerChainEnumerate(
                Controller,
                (uint8_t*)Controller->Scheduler->Periodic.VirtualFrameList[i],
                USB_CHAIN_BREATH,
                HCIPROCESS_REASON_DUMP,
                __DumpScheduleElement,
                &PseudoTransferObject
        );
    }
}

static uint64_t default_dev_hash(const void* deviceIndex)
{
    const struct usb_controller_device_index* index = deviceIndex;
    return index->deviceId;
}

static int default_dev_cmp(const void* deviceIndex1, const void* deviceIndex2)
{
    const struct usb_controller_device_index* index1 = deviceIndex1;
    const struct usb_controller_device_index* index2 = deviceIndex2;
    return index1->deviceId == index2->deviceId ? 0 : 1;
}

static uint64_t endpoint_hash(const void* element)
{
    const struct USBManagerEndpointToggle* endpoint = element;
    return endpoint->Address;
}

static int endpoint_cmp(const void* element1, const void* element2)
{
    const struct USBManagerEndpointToggle* endpoint1 = element1;
    const struct USBManagerEndpointToggle* endpoint2 = element2;
    return endpoint1->Address == endpoint2->Address ? 0 : 1;
}

// Set up the interrupt handler resources for all four USB controller drivers.
// Keep the register region, controller state and event descriptor in the same
// resource-table slots for each driver.
oserr_t
UsbManagerConfigureInterrupt(
    _In_ UsbManagerController_t* controller,
    _In_ InterruptHandler_t     handler,
    _In_ size_t                 controllerSize)
{
    DeviceInterrupt_t* interrupt = &controller->Interrupt;
    uint32_t           count;
    oserr_t            status;

    DeviceInterruptInitialize(interrupt, controller->Device);
    
    // One vector serves the controller's existing event path. The allocator
    // prefers MSI-X, then MSI, and keeps INTx for controllers without either.
    status = DeviceInterruptAllocate(interrupt, 1, 1, &count);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DeviceInterruptSetHandler(interrupt, 0, handler);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DeviceInterruptAddIoResource(interrupt, 0, controller->IoBase);
    if (status != OS_EOK) {
        return status;
    }
    
    status = DeviceInterruptAddMemoryResource(
        interrupt,
        0,
        (uintptr_t)controller,
        controllerSize,
        0
    );
    if (status != OS_EOK) {
        return status;
    }
    return DeviceInterruptSetDescriptor(interrupt, 0, controller->event_descriptor);
}
