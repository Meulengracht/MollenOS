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
#include <ds/hash_sip.h>
#include <event.h>
#include "hci.h"
#include <ioset.h>
#include <io.h>
#include <ioctl.h>
#include <os/usched/job.h>
#include "manager.h"
#include <stdlib.h>

#include "ctt_driver_service_server.h"
#include "ctt_usbhost_service_server.h"

extern gracht_server_t* __crt_get_module_server(void);

struct usb_controller_device_index {
    uuid_t                  deviceId;
    UsbManagerController_t* pointer;
};

struct usb_controller_endpoint {
    uuid_t address;
    int    toggle;
};

static void UsbManagerQueryUHCIPorts(void*);
static void __UHCIPortMonitor(void*,void*);

static uint64_t default_dev_hash(const void*);
static int      default_dev_cmp(const void*, const void*);

static uint64_t endpoint_hash(const void*);
static int      endpoint_cmp(const void*, const void*);

static int           g_hciCheckupRegistered = 0;
static uint8_t       g_hashKey[16]          = { 196, 179, 43, 202, 48, 240, 236, 199, 229, 122, 94, 143, 20, 251, 63, 66 };
static hashtable_t   g_controllers;

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
    _In_ BusDevice_t*        device,
    _In_ UsbControllerType_t type,
    _In_ size_t              structureSize)
{
    UsbManagerController_t* controller;
    int                     opt = 1;

    controller = (UsbManagerController_t*)malloc(structureSize);
    if (!controller) {
        return NULL;
    }
    memset(controller, 0, structureSize);

    controller->Device = device;
    controller->Type = type;
    list_construct(&controller->TransactionList);
    hashtable_construct(
            &controller->Endpoints,
            0,
            sizeof(struct usb_controller_endpoint),
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
    if (type == UsbUHCI && !g_hciCheckupRegistered) {
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
            controller->Type,
            controller->PortCount
    );
    if (oserr != OS_EOK) {
        ERROR("[UsbManagerRegisterController] failed with code %u", oserr);
    }
    return oserr;
}

oserr_t
UsbManagerDestroyController(
    _In_ UsbManagerController_t* controller)
{
    // clear the lists
    UsbManagerClearTransfers(controller);

    // remove the controller indexes
    hashtable_remove(&g_controllers, &(struct usb_controller_device_index) {
            .deviceId = controller->Device->Base.Id });

    // clean up resources
    hashtable_destroy(&controller->Endpoints);

    // remove the event descriptor to the gracht server
    ioset_ctrl(gracht_server_get_aio_handle(__crt_get_module_server()),
               IOSET_DEL, controller->event_descriptor, NULL);
    close(controller->event_descriptor);

    // Unregister controller with usbmanager service
    return UsbControllerUnregister(controller->Device->Base.Id);
}

static void
UsbManagerQueryUHCIController(
    _In_ int         index,
    _In_ const void* element,
    _In_ void*       unusedContext)
{
    const struct usb_controller_device_index* deviceIndex = element;
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
            UsbManagerDestroyTransfer(transfer);
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
        _In_ uuid_t          deviceId,
        _In_ UsbHcAddress_t* address)
{
    // Create an unique id for this endpoint
    UsbManagerController_t*         controller = UsbManagerGetController(deviceId);
    struct usb_controller_endpoint* endpoint;
    uuid_t                          endpointAddress = ((uint32_t)address->DeviceAddress << 8) | address->EndpointAddress;

    if (!controller) {
        return 0;
    }

    endpoint = hashtable_get(&controller->Endpoints, &(struct usb_controller_endpoint) {
        .address = endpointAddress });
    if (!endpoint) {
        hashtable_set(&controller->Endpoints, &(struct usb_controller_endpoint) {
                .address = endpointAddress, .toggle = 0 });
        return 0;
    }
    return endpoint->toggle;
}

oserr_t
UsbManagerSetToggle(
        _In_ uuid_t          deviceId,
        _In_ UsbHcAddress_t* address,
        _In_ int             toggle)
{
    // Create an unique id for this endpoint
    UsbManagerController_t*         controller = UsbManagerGetController(deviceId);
    struct usb_controller_endpoint* endpoint;
    uuid_t                          endpointAddress = ((uint32_t)address->DeviceAddress << 8) | address->EndpointAddress;

    if (!controller) {
        return OS_ENOENT;
    }

    endpoint = hashtable_get(&controller->Endpoints, &(struct usb_controller_endpoint) {
            .address = endpointAddress });
    if (!endpoint) {
        hashtable_set(&controller->Endpoints, &(struct usb_controller_endpoint) {
                .address = endpointAddress, .toggle = toggle });
        return OS_EOK;
    }

    endpoint->toggle = toggle;
    return OS_EOK;
}

void ctt_usbhost_reset_endpoint_invocation(struct gracht_message* message, const uuid_t deviceId,
        const uint8_t hub, const uint8_t port, const uint8_t device, const uint8_t endpoint)
{
    UsbHcAddress_t address = { hub, port, device, endpoint };
    oserr_t     status  = UsbManagerSetToggle(deviceId, &address, 0);
    ctt_usbhost_reset_endpoint_response(message, status);
}

void
UsbManagerQueueWaitingTransfers(
    _In_ UsbManagerController_t* controller)
{
    foreach(node, &controller->TransactionList) {
        UsbManagerTransfer_t* transfer = (UsbManagerTransfer_t*)node->value;

        if (transfer->Status == TransferQueued) {
            if (transfer->Transfer.Type == USB_TRANSFER_ISOCHRONOUS) {
                HciQueueTransferIsochronous(transfer);
            }
            else {
                HciQueueTransferGeneric(transfer);
            }
            break;
        }
    }
}

oserr_t
UsbManagerFinalizeTransfer(UsbManagerTransfer_t* transfer)
{
    int bytesLeft = 0;
    TRACE("UsbManagerFinalizeTransfer()");

    // Is the transfer only partially done?
    if ((transfer->Transfer.Type == USB_TRANSFER_CONTROL || transfer->Transfer.Type == USB_TRANSFER_BULK)
        && transfer->Status == TransferFinished
        && (transfer->Flags & TransferFlagPartial)
        && !(transfer->Flags & TransferFlagShort)) {
        bytesLeft = 1;
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (bytesLeft == 1) {
        HciQueueTransferGeneric(transfer);
        return OS_EINCOMPLETE;
    }
    else {
        UsbManagerSendNotification(transfer);
        return OS_EOK;
    }
}

static void
ClearTransferCallback(
    _In_ element_t* item,
    _In_ void*      context)
{
    UsbManagerController_t* controller = context;
    UsbManagerTransfer_t*   transfer   = (UsbManagerTransfer_t*)item->value;

    // clear the partial flag
    transfer->Flags &= ~(TransferFlagPartial);

    // finalize the transfer
    HciTransactionFinalize(controller, transfer, 1);
    UsbManagerFinalizeTransfer(transfer);
    UsbManagerDestroyTransfer(transfer);
}

void
UsbManagerClearTransfers(
    _In_ UsbManagerController_t* controller)
{
    list_clear(&controller->TransactionList, ClearTransferCallback, controller);
}

oserr_t
UsbManagerIsAddressesEqual(
    _In_ UsbHcAddress_t* Address1,
    _In_ UsbHcAddress_t* Address2)
{
    if (Address1->HubAddress      == Address2->HubAddress     &&
        Address1->PortAddress     == Address2->PortAddress    && 
        Address1->DeviceAddress   == Address2->DeviceAddress  &&
        Address1->EndpointAddress == Address2->EndpointAddress) {
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}

int
UsbManagerSynchronizeTransfers(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ void*                   Context)
{
    UsbHcAddress_t* Address = (UsbHcAddress_t*)Context;
    TRACE("UsbManagerSynchronizeTransfers()");

    // Is this transfer relevant?
    if (UsbManagerIsAddressesEqual(&Transfer->Transfer.Address, Address) != OS_EOK &&
        Transfer->Status != TransferInProgress &&
        Transfer->Transfer.Type != USB_TRANSFER_BULK &&
        Transfer->Transfer.Type != USB_TRANSFER_INTERRUPT) {
        return ITERATOR_CONTINUE;
    }

    // Synchronize toggles in this transfer
    UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
        USB_CHAIN_DEPTH, USB_REASON_FIXTOGGLE, HciProcessElement, Transfer);
    return ITERATOR_CONTINUE;
}

int
UsbManagerScheduleTransfer(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ void*                   Context)
{
    // Has the transfer been marked for unschedule?
    if (Transfer->Flags & TransferFlagUnschedule) {
        TRACE("UNSCHEDULE(Id %u)", Transfer->Id);
        // Clear flag
        Transfer->Flags &= ~(TransferFlagUnschedule);
        HciTransactionFinalize(Controller, Transfer, 0);
        Transfer->Flags |= TransferFlagCleanup;
    }

    // Has the transfer been marked for schedule?
    if (Transfer->Flags & TransferFlagSchedule) {
        TRACE("SCHEDULE(Id %u)", Transfer->Id);
        // Clear flag
        Transfer->Flags &= ~(TransferFlagSchedule);
        UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
            USB_CHAIN_DEPTH, USB_REASON_LINK, HciProcessElement, Transfer);
    }
    return ITERATOR_CONTINUE;
}

void
UsbManagerScheduleTransfers(
    _In_ UsbManagerController_t* Controller)
{
    UsbManagerIterateTransfers(Controller, UsbManagerScheduleTransfer, NULL);
}

int
UsbManagerProcessTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer,
    _In_ void*                   context)
{
    TRACE("UsbManagerProcessTransfer(controller=0x%" PRIxIN ", transfer=0x%" PRIxIN ", flags=0x%x)",
          controller, transfer, transfer ? transfer->Flags : 0);
    
    // Has the transfer been marked for cleanup?
    if (transfer->Flags & TransferFlagCleanup) {
        if (transfer->EndpointDescriptor != NULL) {
            UsbManagerIterateChain(controller, transfer->EndpointDescriptor,
                                   USB_CHAIN_DEPTH, USB_REASON_CLEANUP, HciProcessElement, transfer);
            transfer->EndpointDescriptor = NULL; // Reset
        }
        if (UsbManagerFinalizeTransfer(transfer) == OS_EOK) {
            return ITERATOR_REMOVE;
        }
        return ITERATOR_CONTINUE;
    }

    // No reason to check for any other processing if it's not queued
    if (transfer->Status != TransferInProgress) {
        return ITERATOR_CONTINUE;
    }
    
    // Debug
    TRACE("> Validation transfer(Id %u, Status %u)", transfer->Id, transfer->Status);
    UsbManagerIterateChain(controller, transfer->EndpointDescriptor,
                           USB_CHAIN_DEPTH, USB_REASON_SCAN, HciProcessElement, transfer);
    TRACE("> Updated metrics (Id %u, Status %u, Flags 0x%x)", transfer->Id, transfer->Status, transfer->Flags);
    if (transfer->Status == TransferInProgress) {
        return ITERATOR_CONTINUE;
    }
    
    // Do we need to fixup toggles?
    if (transfer->Flags & TransferFlagSync) {
        UsbManagerIterateTransfers(controller, UsbManagerSynchronizeTransfers, &transfer->Transfer.Address);
    }

    // Restart?
    if (transfer->Transfer.Type == USB_TRANSFER_INTERRUPT || transfer->Transfer.Type == USB_TRANSFER_ISOCHRONOUS) {
        // In case of stall we need should not restart the transfer, and instead let it sit untill host
        // has reset the endpoint and cleared STALL condition.
        if (transfer->Status != TransferStalled) {
            UsbManagerIterateChain(controller, transfer->EndpointDescriptor,
                                   USB_CHAIN_DEPTH, USB_REASON_RESET, HciProcessElement, transfer);
            HciProcessEvent(controller, USB_EVENT_RESTART_DONE, transfer);
        }

        // Don't notify driver when recieving a NAK response. Simply means device had
        // no data to send us. I just wished that it would leave the data intact instead.
        if (transfer->Status != TransferNAK) {
            UsbManagerSendNotification(transfer);
        }

        if (transfer->Status != TransferStalled) {
            transfer->Status = TransferInProgress;
            transfer->Flags  = TransferFlagNone;
        }
    }
    else if (transfer->Transfer.Type == USB_TRANSFER_CONTROL || transfer->Transfer.Type == USB_TRANSFER_BULK) {
        HciTransactionFinalize(controller, transfer, 0);
        if (!(controller->Scheduler->Settings.Flags & USB_SCHEDULER_DEFERRED_CLEAN)) {
            transfer->EndpointDescriptor = NULL;
            if (UsbManagerFinalizeTransfer(transfer) == OS_EOK) {
                return ITERATOR_REMOVE;
            }
        }
    }
    return ITERATOR_CONTINUE;
}

void
UsbManagerProcessTransfers(
    _In_ UsbManagerController_t* controller)
{
    TRACE("UsbManagerProcessTransfers(controller=0x%" PRIxIN ")", controller);
    UsbManagerIterateTransfers(controller, UsbManagerProcessTransfer, NULL);
    UsbManagerQueueWaitingTransfers(controller);
}

void
UsbManagerIterateChain(
    _In_ UsbManagerController_t*     Controller,
    _In_ uint8_t*                    ElementRoot,
    _In_ int                         Direction,
    _In_ int                         Reason,
    _In_ UsbSchedulerElementCallback ElementCallback,
    _In_ void*                       Context)
{
    UsbSchedulerObject_t* object  = NULL;
    UsbSchedulerPool_t*   pool    = NULL;
    uint8_t*              element = ElementRoot;
    oserr_t               oserr;
    uint16_t              rootIndex;
    uint16_t              linkIndex;
    int                   status;
    
    // Debug
    assert(Controller != NULL);
    assert(ElementRoot != NULL);

    // Validate element and lookup pool
    oserr = UsbSchedulerGetPoolFromElement(
            Controller->Scheduler,
            element,
            &pool
    );
    if (oserr != OS_EOK) {
        WARNING("UsbManagerIterateChain: cannot get object from pool");
        return;
    }
    object = USB_ELEMENT_OBJECT(pool, element);
    
    // Get indices
    rootIndex = object->Index;
    linkIndex = (Direction == USB_CHAIN_BREATH) ? object->BreathIndex : object->DepthIndex;

    // Iterate to end, support cyclic queues
    while (element) {
        // Support null-elements
        if (Controller->Scheduler->Settings.Flags & USB_SCHEDULER_NULL_ELEMENT) {
            if (linkIndex == USB_ELEMENT_NO_INDEX) {
                break;
            }
        }

        status = ElementCallback(Controller, element, Reason, Context);
        if (status & ITERATOR_STOP) {
            break;
        }
        if (status & ITERATOR_REMOVE) {
            WARNING("ElementCallback returned ITERATOR_REMOVE");
        }
        
        // Should we stop?
        if (linkIndex == USB_ELEMENT_NO_INDEX || linkIndex == rootIndex) {
            break;
        }
        
        // Move to next object
        pool      = USB_ELEMENT_GET_POOL(Controller->Scheduler, linkIndex);
        element   = USB_ELEMENT_INDEX(pool, linkIndex);
        object    = USB_ELEMENT_OBJECT(pool, element);
        linkIndex = (Direction == USB_CHAIN_BREATH) ? object->BreathIndex : object->DepthIndex;
    }
}

void
UsbManagerDumpChain(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ uint8_t*                ElementRoot,
    _In_ int                     Direction)
{
    // Invoke HciProcessElement
    UsbManagerIterateChain(Controller, ElementRoot, 
        Direction, USB_REASON_DUMP, HciProcessElement, Transfer);
}

int
UsbManagerDumpScheduleElement(
    _In_ UsbManagerController_t* Controller,
    _In_ uint8_t*                Element,
    _In_ int                     Reason,
    _In_ void*                   Context)
{
    UsbManagerTransfer_t* Transfer = (UsbManagerTransfer_t*)Context;
    Transfer->EndpointDescriptor   = Element;
    
    // Dump entire depth chain
    UsbManagerIterateChain(Controller, Element, 
        USB_CHAIN_DEPTH, USB_REASON_DUMP, HciProcessElement, Context);
    return ITERATOR_CONTINUE;
}

void
UsbManagerDumpSchedule(
    _In_ UsbManagerController_t* Controller)
{
    UsbManagerTransfer_t PseudoTransferObject = { { { 0 } } };
    for (int i = 0; i < Controller->Scheduler->Settings.FrameCount; i++) {
        WARNING("-------------------------- FRAME %i ---------------------------------", i);
        UsbManagerIterateChain(Controller, (uint8_t*)Controller->Scheduler->VirtualFrameList[i], 
            USB_CHAIN_BREATH, USB_REASON_DUMP, UsbManagerDumpScheduleElement, &PseudoTransferObject);
    }
}

static uint64_t default_dev_hash(const void* deviceIndex)
{
    const struct usb_controller_device_index* index = deviceIndex;
    return siphash_64((const uint8_t*)&index->deviceId, sizeof(uuid_t), &g_hashKey[0]);
}

static int default_dev_cmp(const void* deviceIndex1, const void* deviceIndex2)
{
    const struct usb_controller_device_index* index1 = deviceIndex1;
    const struct usb_controller_device_index* index2 = deviceIndex2;
    return index1->deviceId == index2->deviceId ? 0 : 1;
}

static uint64_t endpoint_hash(const void* element)
{
    const struct usb_controller_endpoint* endpoint = element;
    return siphash_64((const uint8_t*)&endpoint->address, sizeof(uuid_t), &g_hashKey[0]);
}

static int endpoint_cmp(const void* element1, const void* element2)
{
    const struct usb_controller_endpoint* endpoint1 = element1;
    const struct usb_controller_endpoint* endpoint2 = element2;
    return endpoint1->address == endpoint2->address ? 0 : 1;
}
