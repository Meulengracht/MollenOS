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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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
#include "manager.h"
#include <stdlib.h>
#include <threads.h>

#include "ctt_driver_protocol_server.h"
#include "ctt_usbhost_protocol_server.h"

struct usb_controller_device_index {
    UUId_t                  deviceId;
    UsbManagerController_t* pointer;
};

struct usb_controller_iod_index {
    int                     iod;
    UsbManagerController_t* pointer;
};

struct usb_controller_endpoint {
    UUId_t address;
    int    toggle;
};

static uint64_t default_dev_hash(const void*);
static int      default_dev_cmp(const void*, const void*);

static uint64_t default_iod_hash(const void*);
static int      default_iod_cmp(const void*, const void*);

static uint64_t endpoint_hash(const void*);
static int      endpoint_cmp(const void*, const void*);

static EventQueue_t* eventQueue = NULL;
static hashtable_t   controllers;
static hashtable_t   controllersByIod;

OsStatus_t
UsbManagerInitialize(void)
{
    // Create the event queue and wait for usb services, give it
    // up to 5 seconds before appearing
    if (WaitForUsbService(5000) != OsSuccess) {
        ERROR(" => Failed to start usb manager, as usb service never became available.");
        return OsTimeout;
    }

    CreateEventQueue(&eventQueue);
    hashtable_construct(&controllers, 0,
            sizeof(struct usb_controller_device_index),
            default_dev_hash, default_dev_cmp);
    hashtable_construct(&controllersByIod, 0,
            sizeof(struct usb_controller_iod_index),
            default_iod_hash, default_iod_cmp);

    return OsSuccess;
}

void
UsbManagerDestroy(void)
{
    DestroyEventQueue(eventQueue);
    hashtable_destroy(&controllersByIod);
    hashtable_destroy(&controllers);
}

UsbManagerController_t*
UsbManagerCreateController(
    _In_ BusDevice_t*        device,
    _In_ UsbControllerType_t type,
    _In_ size_t              structureSize)
{
    UsbManagerController_t* controller;

    controller = (UsbManagerController_t*)malloc(structureSize);
    if (!controller) {
        return NULL;
    }

    memset(controller, 0, structureSize);
    memcpy(&controller->Device, device, device->Base.Length);

    controller->Type = type;
    list_construct(&controller->TransactionList);
    hashtable_construct(&controller->Endpoints, 0,
            sizeof(UsbManagerEndpoint_t), endpoint_hash, endpoint_cmp);
    spinlock_init(&controller->Lock, spinlock_plain);

    // create the event descriptor to allow listening for interrupts
    controller->event_descriptor = eventd(0, EVT_RESET_EVENT);
    if (controller->event_descriptor < 0) {
        free(controller);
        return NULL;
    }

    // add the event descriptor to the gracht server
    ioset_ctrl(gracht_server_get_set_iod(), IOSET_ADD, controller->event_descriptor,
        &(struct ioset_event){ .data.iod = controller->event_descriptor, .events = IOSETSYN });

    // add indexes
    hashtable_set(&controllers, &(struct usb_controller_device_index) {
        .deviceId = device->Base.Id, .pointer = controller });
    hashtable_set(&controllersByIod, &(struct usb_controller_iod_index) {
        .iod = controller->event_descriptor, .pointer = controller });
    return controller;
}

OsStatus_t
UsbManagerRegisterController(
    _In_ UsbManagerController_t* controller)
{
    OsStatus_t status = UsbControllerRegister(&controller->Device.Base, controller->Type, controller->PortCount);
    if (status != OsSuccess) {
        ERROR("[UsbManagerRegisterController] failed with code %u", status);
    }
    return status;
}

OsStatus_t
UsbManagerDestroyController(
    _In_ UsbManagerController_t* controller)
{
    // clear the lists
    UsbManagerClearTransfers(controller);

    // remove the controller indexes
    hashtable_remove(&controllers, &(struct usb_controller_device_index) {
            .deviceId = controller->Device.Base.Id });
    hashtable_remove(&controllersByIod, &(struct usb_controller_iod_index) {
            .iod = controller->event_descriptor });

    // clean up resources
    hashtable_destroy(&controller->Endpoints);

    // remove the event descriptor to the gracht server
    ioset_ctrl(gracht_server_get_set_iod(), IOSET_DEL, controller->event_descriptor, NULL);
    close(controller->event_descriptor);

    // Unregister controller with usbmanager service
    return UsbControllerUnregister(controller->Device.Base.Id);
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
            list_remove(&controller->TransactionList, node);
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
UsbManagerGetControllerByDeviceId(
    _In_ UUId_t id)
{
    return (UsbManagerController_t*)hashtable_get(&controllers,
            &(struct usb_controller_device_index) { .deviceId = id });
}

UsbManagerController_t*
UsbManagerGetControllerByIod(
        _In_ int iod)
{
    return (UsbManagerController_t*)hashtable_get(&controllersByIod,
            &(struct usb_controller_iod_index) { .iod = iod });
}

int
UsbManagerGetToggle(
    _In_ UUId_t          deviceId,
    _In_ UsbHcAddress_t* address)
{
    // Create an unique id for this endpoint
    UsbManagerController_t*         controller;
    struct usb_controller_endpoint* endpoint;
    UUId_t                          endpointAddress = ((uint32_t)address->DeviceAddress << 8) | address->EndpointAddress;

    controller = hashtable_get(&controllers, &(struct usb_controller_device_index) { .deviceId = deviceId });
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

OsStatus_t
UsbManagerSetToggle(
    _In_ UUId_t          deviceId,
    _In_ UsbHcAddress_t* address,
    _In_ int             toggle)
{
    // Create an unique id for this endpoint
    UsbManagerController_t*         controller;
    struct usb_controller_endpoint* endpoint;
    UUId_t                          endpointAddress = ((uint32_t)address->DeviceAddress << 8) | address->EndpointAddress;

    controller = hashtable_get(&controllers, &(struct usb_controller_device_index) { .deviceId = deviceId });
    if (!controller) {
        return OsDoesNotExist;
    }

    endpoint = hashtable_get(&controller->Endpoints, &(struct usb_controller_endpoint) {
            .address = endpointAddress });
    if (!endpoint) {
        hashtable_set(&controller->Endpoints, &(struct usb_controller_endpoint) {
                .address = endpointAddress, .toggle = toggle });
        return OsSuccess;
    }

    endpoint->toggle = toggle;
    return OsSuccess;
}

void ctt_usbhost_reset_endpoint_callback(struct gracht_recv_message* message, struct ctt_usbhost_reset_endpoint_args* args)
{
    UsbHcAddress_t address = { args->hub_address, args->port_address, args->device_address, args->endpoint_address };
    OsStatus_t     status  = UsbManagerSetToggle(args->device_id, &address, 0);
    ctt_usbhost_reset_endpoint_response(message, status);
}

void
UsbManagerQueueWaitingTransfers(
    _In_ UsbManagerController_t* controller)
{
    foreach(node, &controller->TransactionList) {
        UsbManagerTransfer_t* transfer = (UsbManagerTransfer_t*)node->value;

        if (transfer->Status == TransferNotProcessed) {
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

OsStatus_t
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
        return OsIncomplete;
    }
    else {
        UsbManagerSendNotification(transfer);
        return OsSuccess;
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

OsStatus_t
UsbManagerIsAddressesEqual(
    _In_ UsbHcAddress_t* Address1,
    _In_ UsbHcAddress_t* Address2)
{
    if (Address1->HubAddress      == Address2->HubAddress     &&
        Address1->PortAddress     == Address2->PortAddress    && 
        Address1->DeviceAddress   == Address2->DeviceAddress  &&
        Address1->EndpointAddress == Address2->EndpointAddress) {
        return OsSuccess;
    }
    return OsError;
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
    if (UsbManagerIsAddressesEqual(&Transfer->Transfer.Address, Address) != OsSuccess && 
        Transfer->Status != TransferQueued && 
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
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ void*                   Context)
{
    TRACE("UsbManagerProcessTransfer()");
    
    // Has the transfer been marked for cleanup?
    if (Transfer->Flags & TransferFlagCleanup) {
        if (Transfer->EndpointDescriptor != NULL) {
            UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
                USB_CHAIN_DEPTH, USB_REASON_CLEANUP, HciProcessElement, Transfer);
            Transfer->EndpointDescriptor = NULL; // Reset
        }
        if (UsbManagerFinalizeTransfer(Transfer) == OsSuccess) {
            return ITERATOR_REMOVE;
        }
        return ITERATOR_CONTINUE;
    }

    // No reason to check for any other processing if it's not queued
    if (Transfer->Status != TransferQueued) {
        return ITERATOR_CONTINUE;
    }
    
    // Debug
    TRACE("> Validation transfer(Id %u, Status %u)", Transfer->Id, Transfer->Status);
    UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
        USB_CHAIN_DEPTH, USB_REASON_SCAN, HciProcessElement, Transfer);
    TRACE("> Updated metrics (Id %u, Status %u, Flags 0x%x)", Transfer->Id, Transfer->Status, Transfer->Flags);
    if (Transfer->Status == TransferQueued) {
        return ITERATOR_CONTINUE;
    }
    
    // Do we need to fixup toggles?
    if (Transfer->Flags & TransferFlagSync) {
        UsbManagerIterateTransfers(Controller, UsbManagerSynchronizeTransfers, &Transfer->Transfer.Address);
    }

    // Restart?
    if (Transfer->Transfer.Type == USB_TRANSFER_INTERRUPT || Transfer->Transfer.Type == USB_TRANSFER_ISOCHRONOUS) {
        UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
            USB_CHAIN_DEPTH, USB_REASON_RESET, HciProcessElement, Transfer);
        HciProcessEvent(Controller, USB_EVENT_RESTART_DONE, Transfer);

        // Don't notify driver when recieving a NAK response. Simply means device had
        // no data to send us. I just wished that it would leave the data intact instead.
        if (Transfer->Status != TransferNAK) {
            UsbManagerSendNotification(Transfer);
        }
        Transfer->Status = TransferQueued;
        Transfer->Flags  = TransferFlagNone;
    }
    else if (Transfer->Transfer.Type == USB_TRANSFER_CONTROL || Transfer->Transfer.Type == USB_TRANSFER_BULK) {
        HciTransactionFinalize(Controller, Transfer, 0);
        if (!(Controller->Scheduler->Settings.Flags & USB_SCHEDULER_DEFERRED_CLEAN)) {
            Transfer->EndpointDescriptor = NULL;
            if (UsbManagerFinalizeTransfer(Transfer) == OsSuccess) {
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
    UsbSchedulerObject_t* Object  = NULL;
    UsbSchedulerPool_t*   Pool    = NULL;
    uint8_t*              Element = ElementRoot;
    OsStatus_t            Result;
    uint16_t              RootIndex;
    uint16_t              LinkIndex;
    int                   Status;
    
    // Debug
    assert(Controller != NULL);
    assert(ElementRoot != NULL);

    // Validate element and lookup pool
    Result = UsbSchedulerGetPoolFromElement(Controller->Scheduler, Element, &Pool);
    assert(Result == OsSuccess);
    Object = USB_ELEMENT_OBJECT(Pool, Element);
    
    // Get indices
    RootIndex = Object->Index;
    LinkIndex = (Direction == USB_CHAIN_BREATH) ? Object->BreathIndex : Object->DepthIndex;

    // Iterate to end, support cyclic queues
    while (Element) {
        // Support null-elements
        if (Controller->Scheduler->Settings.Flags & USB_SCHEDULER_NULL_ELEMENT) {
            if (LinkIndex == USB_ELEMENT_NO_INDEX) {
                break;
            }
        }
        
        Status = ElementCallback(Controller, Element, Reason, Context);
        if (Status & ITERATOR_STOP) {
            break;
        }
        if (Status & ITERATOR_REMOVE) {
            WARNING("ElementCallback returned ITERATOR_REMOVE");
        }
        
        // Should we stop?
        if (LinkIndex == USB_ELEMENT_NO_INDEX || LinkIndex == RootIndex) {
            break;
        }
        
        // Move to next object
        Pool      = USB_ELEMENT_GET_POOL(Controller->Scheduler, LinkIndex);
        Element   = USB_ELEMENT_INDEX(Pool, LinkIndex);
        Object    = USB_ELEMENT_OBJECT(Pool, Element);
        LinkIndex = (Direction == USB_CHAIN_BREATH) ? Object->BreathIndex : Object->DepthIndex;
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
