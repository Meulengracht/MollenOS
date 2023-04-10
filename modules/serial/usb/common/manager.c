/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */

//#define __TRACE

#include <assert.h>
#include <ddk/service.h>
#include <ddk/utils.h>
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

static void __UHCIPortMonitor(void*,void*);

static uint64_t default_dev_hash(const void*);
static int      default_dev_cmp(const void*, const void*);

static uint64_t endpoint_hash(const void*);
static int      endpoint_cmp(const void*, const void*);

static int         g_hciCheckupRegistered = 0;
static hashtable_t g_controllers;

oserr_t
UsbManagerInitialize(void)
{
    // Create the event queue and wait for usb services, give it
    // up to 5 seconds before appearing
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
    _In_ BusDevice_t*           device,
    _In_ enum USBControllerKind kind,
    _In_ size_t                 structureSize)
{
    UsbManagerController_t* controller;
    int                     opt = 1;

    controller = (UsbManagerController_t*)malloc(structureSize);
    if (!controller) {
        return NULL;
    }
    memset(controller, 0, structureSize);

    controller->Device = device;
    controller->Kind = kind;
    list_construct(&controller->TransactionList);
    hashtable_construct(
            &controller->Endpoints,
            0,
            sizeof(struct USBManagerEndpointToggle),
                    endpoint_hash,
                    endpoint_cmp
    );
    spinlock_init(&controller->Lock);

    // create the event descriptor to allow listening for interrupts
    controller->event_descriptor = eventd(0, EVT_RESET_EVENT);
    if (controller->event_descriptor < 0) {
        free(controller);
        return NULL;
    }

    // add the event descriptor to the gracht server, and then we would like to set it non-blocking
    ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
               IOSET_ADD, controller->event_descriptor,
               &(struct ioset_event){ .data.context = controller, .events = IOSETSYN });
    ioctl(controller->event_descriptor, FIONBIO, &opt);

    // add indexes
    hashtable_set(&g_controllers, &(struct usb_controller_device_index) {
        .deviceId = device->Base.Id, .pointer = controller });

    // UHCI does not support hub events, so we install a timer if not already
    if (kind == USBCONTROLLER_KIND_OHCI && !g_hciCheckupRegistered) {
        usched_job_queue(__UHCIPortMonitor, NULL);
        g_hciCheckupRegistered = 1;
    }
    return controller;
}

oserr_t
UsbManagerRegisterController(
    _In_ UsbManagerController_t* controller)
{
    oserr_t oserr = UsbControllerRegister(
            &controller->Device->Base,
            controller->Kind,
            (int)controller->PortCount
    );
    if (oserr != OS_EOK) {
        ERROR("[UsbManagerRegisterController] failed with code %u", oserr);
    }
    return oserr;
}

void
UsbManagerDestroyController(
    _In_ UsbManagerController_t* controller)
{
    // clear the lists
    UsbManagerClearTransfers(controller);

    // remove the controller indexes
    hashtable_remove(
            &g_controllers,
            &(struct usb_controller_device_index) {
                .deviceId = controller->Device->Base.Id
            }
    );

    // clean up resources
    hashtable_destroy(&controller->Endpoints);

    // remove the event descriptor to the gracht server
    ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
               IOSET_DEL, controller->event_descriptor, NULL);
    close(controller->event_descriptor);

    // Unregister controller with usbmanager service
    UsbControllerUnregister(controller->Device->Base.Id);
}

static void
UsbManagerQueryUHCIController(
    _In_ int         index,
    _In_ const void* element,
    _In_ void*       context)
{
    const struct usb_controller_device_index* deviceIndex = element;
    _CRT_UNUSED(index);
    _CRT_UNUSED(context);
    HciTimerCallback(deviceIndex->pointer);
}

static void
__UHCIPortMonitor(
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
                UsbManagerQueryUHCIController,
                NULL
        );
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
            USBTransferDestroy(transfer);
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

void ctt_usbhost_reset_endpoint_invocation(struct gracht_message* message, const uuid_t deviceId,
        const uint8_t hub, const uint8_t port, const uint8_t device, const uint8_t endpoint)
{
    USBAddress_t address = {hub, port, device, endpoint };
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    if (controller == NULL) {
        ctt_usbhost_reset_endpoint_response(message, OS_ENOENT);
        return;
    }
    UsbManagerSetToggle(controller, &address, 0);
    ctt_usbhost_reset_endpoint_response(message, OS_EOK);
}

static void
__QueueWaitingTransfers(
    _In_ UsbManagerController_t* controller)
{
    foreach(node, &controller->TransactionList) {
        UsbManagerTransfer_t* transfer = (UsbManagerTransfer_t*)node->value;

        if (transfer->State == USBTRANSFER_STATE_WAITING) {
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
    bool isPartial = true;
    TRACE("__FinalizeTransfer()");

    // Is the transfer only partially done?
    if (__Transfer_IsAsync(transfer)) {
        isPartial = __Transfer_IsPartial(transfer);
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (isPartial) {
        HCITransferQueue(transfer);
        return OS_EINCOMPLETE;
    }

    USBTransferNotify(transfer);
    return OS_EOK;
}

static void
__ClearTransfer(
    _In_ element_t* item,
    _In_ void*      context)
{
    UsbManagerController_t* controller = context;
    UsbManagerTransfer_t*   transfer   = (UsbManagerTransfer_t*)item->value;

    // Mark the transfer as cancelled, to let users know that their request
    // cannot be met at this time.
    transfer->ResultCode = USBTRANSFERCODE_CANCELLED;

    // finalize the transfer
    HciTransactionFinalize(controller, transfer, 1);
    (void)__FinalizeTransfer(transfer);
    USBTransferDestroy(transfer);
}

void
UsbManagerClearTransfers(
    _In_ UsbManagerController_t* controller)
{
    list_clear(&controller->TransactionList, __ClearTransfer, controller);
}

static int
__ScheduleTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ void*                   context)
{
    _CRT_UNUSED(context);

    if (transfer->State == USBTRANSFER_STATE_UNSCHEDULE) {
        TRACE("__ScheduleTransfer: [unschedule] transferID=%u", transfer->ID);
        HciTransactionFinalize(controller, transfer, 0);
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
__ProcessCleanup(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer)
{
    // If there is an endpoint descriptor, then we free it and mark
    // it NULL to indicate a new must be allocated. We do this for convenience
    // to avoid having extra code to detect whether we have to allocate one.
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

    // Try to finalize the transfer, if this does not return OS_EOK,
    // it means the transfer wasn't finished, but maybe only partially
    // finished, so we cannot clean it.
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
    // If the transfer is still not accessed, then it's not done.
    if (scanContext->Result == USBTRANSFERCODE_INVALID) {
        return false;
    }

    // If the transfer is in error-state, then it's done.
    if (scanContext->Result != USBTRANSFERCODE_SUCCESS) {
        return true;
    }

    // If the transfer was short, then it's by default also done. Nothing
    // more to be executed at this point.
    if (scanContext->Short) {
        return true;
    }

    // If the transfer is OK and not short, then check whether all the
    // elements has executed
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
__CheckTransfer(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer)
{
    struct HCIProcessReasonScanContext context = {
            .Transfer = transfer,
    };
    TRACE("__ProcessTransfer: starting scan id=%u, status=%u", transfer->ID, transfer->Status);
    UsbManagerChainEnumerate(
            controller,
            transfer->RootElement,
            USB_CHAIN_DEPTH,
            HCIPROCESS_REASON_SCAN,
            HCIProcessElement,
            &context
    );
    TRACE("__ProcessTransfer: finished scan status=%u, flags=0x%x", transfer->Status, transfer->Flags);

    // Increase metrics for async transfers
    if (__Transfer_IsAsync(transfer)) {
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

    // Update result-code for transfer, it's needed to report back to
    // the subscribers, and determine completion status.
    transfer->ResultCode = context.Result;
    if (context.Short) {
        transfer->Flags |= __USBTRANSFER_FLAG_SHORT;
    }

    // On error transfers, or short transfers, the toggle may end up
    // being invalid, if some processing has taken place. Update the stored
    // toggle in preparation of failing transfers.
    if ((context.Result != USBTRANSFERCODE_SUCCESS || context.Short) &&
        context.ElementsProcessed > 0) {
         UsbManagerSetToggle(controller, &transfer->Address, context.LastToggle);
    }

    // For INT and ISOC we support restarting of transfers.
    if (__Transfer_IsPeriodic(transfer)) {
        // In case of stall we need should not restart the transfer, and instead let it sit untill host
        // has reset the endpoint and cleared STALL condition.
        if (context.Result != TransferStalled) {
            __ResetTransfer(controller, transfer);
        }

        // Don't notify driver when recieving a NAK response. Simply means device had
        // no data to send us. I just wished that it would leave the data intact instead.
        if (context.Result != TransferNAK) {
            USBTransferNotify(transfer);
        }

        if (context.Result != TransferStalled) {
            __Transfer_Reset(transfer);
        }
    } else if (__Transfer_IsAsync(transfer)) {
        HciTransactionFinalize(controller, transfer, 0);
        if (!(controller->Scheduler->Settings.Flags & USB_SCHEDULER_DEFERRED_CLEAN)) {
            transfer->RootElement = NULL;
            if (__FinalizeTransfer(transfer) == OS_EOK) {
                return ITERATOR_REMOVE;
            }
        }
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

    switch (transfer->State) {
        case USBTRANSFER_STATE_CREATED:
        case USBTRANSFER_STATE_WAITING:
        case USBTRANSFER_STATE_SCHEDULE:
        case USBTRANSFER_STATE_UNSCHEDULE:
        case USBTRANSFER_STATE_COMPLETED:
            break;
        case USBTRANSFER_STATE_QUEUED:
            return __CheckTransfer(controller, transfer);
        case USBTRANSFER_STATE_CLEANUP:
            return __ProcessCleanup(controller, transfer);
    }
    return ITERATOR_CONTINUE;
}

void
UsbManagerProcessTransfers(
    _In_ UsbManagerController_t* controller)
{
    TRACE("UsbManagerProcessTransfers(controller=0x%" PRIxIN ")", controller);
    UsbManagerIterateTransfers(controller, __ProcessTransfer, NULL);
    __QueueWaitingTransfers(controller);
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

        // Allow the callback to early break by returning false
        if (!elementCallback(controller, element, reason, context)) {
            break;
        }

        // There are two different cases of why we are end of chain
        // 1. We reach a direct end, with no chain element
        // 2. We reach the root index again (head element)
        //    This indicates a cyclic chain, which is valid, but
        //    we need to guard against it.
        if (linkIndex == USB_ELEMENT_NO_INDEX || linkIndex == rootIndex) {
            break;
        }

        // Elements can be linked across pools which makes it impossible to rely
        // on the pool being identical, make sure we retrieve the correct pool
        // on each link
        pool = USB_ELEMENT_GET_POOL(controller->Scheduler, linkIndex);

        // Lookup the correct element in that pool, and then get the UsbSchedulerObject_t
        // instance of that element.
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
                (uint8_t*) Controller->Scheduler->VirtualFrameList[i],
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
