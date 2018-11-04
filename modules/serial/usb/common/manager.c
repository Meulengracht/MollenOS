/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS MCore - USB Controller Manager
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <os/timers.h>
#include <os/utils.h>
#include "manager.h"
#include "hci.h"

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

/* Globals
 * Keeps track of the usb-manager state and its data */
static Collection_t *__GlbControllers   = NULL;
static UUId_t __GlbTimerEvent           = UUID_INVALID;
static UsbCallback __GlbTimerCallback   = NULL;

/* OnTimeout
 * Is called when one of the registered timer-handles
 * times-out. A new timeout event is generated and passed
 * on to the below handler */
OsStatus_t
OnTimeout(
    _In_ UUId_t Timer,
    _In_ void*  Data) {
    _CRT_UNUSED(Timer); _CRT_UNUSED(Data);
    if (__GlbTimerCallback != NULL) {
        __GlbTimerCallback();
    }
    return OsSuccess;
}

/* UsbManagerInitialize
 * Initializes the usb manager that keeps track of
 * all controllers and all attached devices */
OsStatus_t
UsbManagerInitialize(void) {
    __GlbControllers    = CollectionCreate(KeyId);
    __GlbTimerEvent     = UUID_INVALID;
    __GlbTimerCallback  = NULL;
    return OsSuccess;
}

/* UsbManagerDestroy
 * Cleans up the manager and releases resources allocated */
OsStatus_t
UsbManagerDestroy(void) {
    foreach(cNode, __GlbControllers) {
        free(cNode->Data);
    }
    return CollectionDestroy(__GlbControllers);
}

/* UsbManagerGetControllers
 * Retrieve a list of all attached controllers to the system. */
Collection_t*
UsbManagerGetControllers(void) {
    return __GlbControllers;
}

/* UsbManagerRegisterTimer
 * Registers a timer function that will be called every given interval. */
OsStatus_t
UsbManagerRegisterTimer(
    _In_ int                        IntervalMs,
    _In_ UsbCallback                Function)
{
    __GlbTimerEvent     = TimerStart(IntervalMs, 1, NULL);
    __GlbTimerCallback  = Function;
    return OsSuccess;
}

/* UsbManagerRegisterController
 * Registers the usb controller with the system. */
OsStatus_t
UsbManagerRegisterController(
    _In_ UsbManagerController_t*    Controller)
{
    DataKey_t   Key = { .Value.Id = Controller->Device.Id };
    int         Retries = 3;
    OsStatus_t  Status = OsError;

    // Register controller with usbmanager service, sometimes the usb service is a tad
    // slow in starting up, so try 3 times, with 1 second between
    for (int i = 0; i < Retries; i++) {
        Status = UsbControllerRegister(&Controller->Device, Controller->Type, Controller->PortCount);
        if (Status == OsSuccess) {
            break;
        }
        thrd_sleepex(1000);
    }
    if (Status != OsSuccess) {
        return Status;
    }
    return CollectionAppend(__GlbControllers, CollectionCreateNode(Key, Controller));
}

/* UsbManagerDestroyController
 * Unregisters a controller with the usb-manager.
 * Identifies and unregisters with neccessary services */
OsStatus_t
UsbManagerDestroyController(
    _In_ UsbManagerController_t*    Controller)
{
    // Variables
    CollectionItem_t *cNode = NULL;
    DataKey_t Key = { .Value.Id = Controller->Device.Id };

    // Unregister controller with usbmanager service
    if (UsbControllerUnregister(Controller->Device.Id) != OsSuccess) {
        return OsError;
    }

    // Remove from list
    cNode = CollectionGetNodeByKey(__GlbControllers, Key, 0);
    if (cNode != NULL) {
        CollectionUnlinkNode(__GlbControllers, cNode);
        return CollectionDestroyNode(__GlbControllers, cNode);
    }
    return OsError;
}

/* UsbManagerIterateTransfers
 * Iterate the transfers associated with the given controller. The iteration
 * flow can be controlled with the return codes. */
void
UsbManagerIterateTransfers(
    _In_ UsbManagerController_t*    Controller,
    _In_ UsbTransferItemCallback    ItemCallback,
    _In_ void*                      Context)
{
    foreach_nolink(Node, Controller->TransactionList) {
        UsbManagerTransfer_t *Transfer  = (UsbManagerTransfer_t*)Node->Data;
        int Status                      = ItemCallback(Controller, Transfer, Context);
        if (Status & ITERATOR_REMOVE) {
            CollectionItem_t *NextNode  = CollectionUnlinkNode(Controller->TransactionList, Node);
            CollectionDestroyNode(Controller->TransactionList, Node);
            Node                        = NextNode;
        }
        else {
            Node                        = CollectionNext(Node);
        }
        if (Status & ITERATOR_STOP) {
            break;
        }
    }
}

/* UsbManagerGetController 
 * Returns a controller by the given device-id */
UsbManagerController_t*
UsbManagerGetController(
    _In_ UUId_t                     Device)
{
    // Iterate list of controllers
    foreach(cNode, __GlbControllers) {
        UsbManagerController_t *Controller = (UsbManagerController_t*)cNode->Data;
        if (Controller->Device.Id == Device) {
            return Controller;
        }
    }
    return NULL;
}

/* UsbManagerGetToggle 
 * Retrieves the toggle status for a given pipe */
int
UsbManagerGetToggle(
    _In_ UUId_t                     Device,
    _In_ UsbHcAddress_t*            Address)
{
    // Variables
    DataKey_t Key;
    UUId_t Pipe;

    // Create an unique id for this endpoint
    Pipe = ((uint32_t)Address->DeviceAddress << 8)  | Address->EndpointAddress;
    Key.Value.Integer = (int)Pipe;

    // Iterate list of controllers
    foreach(cNode, __GlbControllers) {
        // Cast data of node to our type
        UsbManagerController_t *Controller = (UsbManagerController_t*)cNode->Data;
        if (Controller->Device.Id == Device) {
            // Locate the correct endpoint
            foreach(eNode, Controller->Endpoints) {
                // Cast data again
                UsbManagerEndpoint_t *Endpoint = (UsbManagerEndpoint_t*)eNode->Data;
                if (Endpoint->Pipe == Pipe) {
                    return Endpoint->Toggle;
                }
            }
        }
    }

    // Not found, create a new endpoint with toggle 0
    // Iterate list of controllers
    _foreach(cNode, __GlbControllers) {
        // Cast data of node to our type
        UsbManagerController_t *Controller = (UsbManagerController_t*)cNode->Data;
        if (Controller->Device.Id == Device) {
            // Create the endpoint
            UsbManagerEndpoint_t *Endpoint = NULL;
            Endpoint = (UsbManagerEndpoint_t*)malloc(sizeof(UsbManagerEndpoint_t));
            Endpoint->Pipe = Pipe;
            Endpoint->Toggle = 0;

            // Add it to the list
            CollectionAppend(Controller->Endpoints, CollectionCreateNode(Key, Endpoint));
        }
    }
    return 0;
}

/* UsbManagetSetToggle 
 * Updates the toggle status for a given pipe */
OsStatus_t
UsbManagerSetToggle(
    _In_ UUId_t                     Device,
    _In_ UsbHcAddress_t*            Address,
    _In_ int                        Toggle)
{
    // Variables
    DataKey_t Key;
    UUId_t Pipe;

    // Create an unique id for this endpoint
    Pipe = ((uint32_t)Address->DeviceAddress << 8)  | Address->EndpointAddress;
    Key.Value.Integer = (int)Pipe;

    // Iterate list of controllers
    foreach(cNode, __GlbControllers) {
        // Cast data of node to our type
        UsbManagerController_t *Controller = (UsbManagerController_t*)cNode->Data;
        if (Controller->Device.Id == Device) {
            // Locate the correct endpoint
            foreach(eNode, Controller->Endpoints) {
                // Cast data again
                UsbManagerEndpoint_t *Endpoint = (UsbManagerEndpoint_t*)eNode->Data;
                if (Endpoint->Pipe == Pipe) {
                    Endpoint->Toggle = Toggle;
                    return OsSuccess;
                }
            }
        }
    }

    // Not found, create a new endpoint with given toggle
    // Iterate list of controllers
    _foreach(cNode, __GlbControllers) {
        // Cast data of node to our type
        UsbManagerController_t *Controller = (UsbManagerController_t*)cNode->Data;
        if (Controller->Device.Id == Device) {
            // Create the endpoint
            UsbManagerEndpoint_t *Endpoint = NULL;
            Endpoint = (UsbManagerEndpoint_t*)malloc(sizeof(UsbManagerEndpoint_t));
            Endpoint->Pipe      = Pipe;
            Endpoint->Toggle    = Toggle;

            // Add it to the list
            return CollectionAppend(Controller->Endpoints, CollectionCreateNode(Key, Endpoint));
        }
    }
    return OsError;
}

/* UsbManagerFinalizeTransfer
 * Finalizes the transfer by sending an notification, requeuing if all bytes are not
 * transferred or queing a transfer waiting. */
OsStatus_t
UsbManagerFinalizeTransfer(
    _In_ UsbManagerController_t*    Controller,
    _In_ UsbManagerTransfer_t*      Transfer)
{
    // Variables
    CollectionItem_t *Node      = NULL;
    int BytesLeft               = 0;

    // Is the transfer done?
    if ((Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer)
        && Transfer->Status == TransferFinished
        && Transfer->TransactionsExecuted != Transfer->TransactionsTotal
        && !(Transfer->Flags & TransferFlagShort)) {
        BytesLeft = 1;
    }

    // We don't allocate the queue head before the transfer
    // is done, we might not be done yet
    if (BytesLeft == 1) {
        HciQueueTransferGeneric(Transfer);
        return OsError;
    }
    else {
        // Should we notify the user here?...
        UsbManagerSendNotification(Transfer);

        // Now run through transactions and check if any are ready to run
        _foreach(Node, Controller->TransactionList) {
            UsbManagerTransfer_t *NextTransfer = (UsbManagerTransfer_t*)Node->Data;
            if (NextTransfer->Status == TransferNotProcessed) {
                if (NextTransfer->Transfer.Type == IsochronousTransfer) {
                    HciQueueTransferIsochronous(NextTransfer);
                }
                else {
                    HciQueueTransferGeneric(NextTransfer);
                }
                break;
            }
        }
        free(Transfer);
        return OsSuccess;
    }
}

/* UsbManagerClearTransfers
 * Clears all queued transfers by iterating them and invoking Finalize.
 * This will also wake-up waiting processes and tell them it's off. */
void
UsbManagerClearTransfers(
    _In_ UsbManagerController_t*    Controller)
{
    // Avoid requeuing by setting executed == total
    foreach(tNode, Controller->TransactionList) {
        UsbManagerTransfer_t *Transfer = (UsbManagerTransfer_t*)tNode->Data;
        Transfer->TransactionsExecuted = Transfer->TransactionsTotal;
        HciTransactionFinalize(Controller, Transfer, 1);
        UsbManagerFinalizeTransfer(Controller, Transfer);
    }
    CollectionClear(Controller->TransactionList);
}

/* UsbManagerIsAddressesEqual
 * Checks two different usb addreses if they are targetting the same endpoint. */
OsStatus_t
UsbManagerIsAddressesEqual(
    _In_ UsbHcAddress_t*            Address1,
    _In_ UsbHcAddress_t*            Address2)
{
    if (Address1->HubAddress        == Address2->HubAddress     &&
        Address1->PortAddress       == Address2->PortAddress    && 
        Address1->DeviceAddress     == Address2->DeviceAddress  &&
        Address1->EndpointAddress   == Address2->EndpointAddress) {
        return OsSuccess;
    }
    return OsError;
}

/* UsbManagerSynchronizeTransfers
 * Synchronizes transfers, the pipe to synchronize with must be passed in the <Context> parameter. 
 * This only affects Bulk/Interrupt and if their transfer status is Queued. */
int
UsbManagerSynchronizeTransfers(
    _In_ UsbManagerController_t*    Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ void*                      Context)
{
    // Variables
    UsbHcAddress_t *Address = (UsbHcAddress_t*)Context;

    // Is this transfer relevant?
    if (UsbManagerIsAddressesEqual(&Transfer->Transfer.Address, Address) != OsSuccess
        && Transfer->Status != TransferQueued
        && (Transfer->Transfer.Type != BulkTransfer || Transfer->Transfer.Type != InterruptTransfer)) {
        return ITERATOR_CONTINUE;
    }

    // Synchronize toggles in this transfer
    UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
        USB_CHAIN_DEPTH, USB_REASON_FIXTOGGLE, HciProcessElement, Transfer);
    return ITERATOR_CONTINUE;
}

/* UsbManagerScheduleTransfer
 * Processes and validates the given transfer item. If any action needs to be taken
 * this function will handle completion. */
int
UsbManagerScheduleTransfer(
    _In_ UsbManagerController_t*    Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ void*                      Context)
{
    // Has the transfer been marked for unschedule?
    if (Transfer->Flags & TransferFlagUnschedule) {
        // Clear flag
        Transfer->Flags &= ~(TransferFlagUnschedule);
        HciTransactionFinalize(Controller, Transfer, 0);
        Transfer->Flags |= TransferFlagCleanup;
    }

    // Has the transfer been marked for schedule?
    if (Transfer->Flags & TransferFlagSchedule) {
        // Clear flag
        Transfer->Flags &= ~(TransferFlagSchedule);
        UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
            USB_CHAIN_DEPTH, USB_REASON_LINK, HciProcessElement, Transfer);
    }
    return ITERATOR_CONTINUE;
}

/* UsbManagerScheduleTransfers
 * Handles all transfers that are marked for either Schedule or Unscheduling.
 * The iteration process will invoke <HciProcessElement> */
void
UsbManagerScheduleTransfers(
    _In_ UsbManagerController_t*    Controller)
{
    // Simply invoke our iterator with a fixed shared implementation.
    UsbManagerIterateTransfers(Controller, UsbManagerScheduleTransfer, NULL);
}

/* UsbManagerProcessTransfer
 * Processes and validates the given transfer item. If any action needs to be taken
 * this function will handle completion. */
int
UsbManagerProcessTransfer(
    _In_ UsbManagerController_t*    Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ void*                      Context)
{
    // Has the transfer been marked for cleanup?
    if (Transfer->Flags & TransferFlagCleanup) {
        if (Transfer->EndpointDescriptor != NULL) {
            UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
                USB_CHAIN_DEPTH, USB_REASON_CLEANUP, HciProcessElement, Transfer);
            Transfer->EndpointDescriptor = NULL; // Reset
        }
        if (UsbManagerFinalizeTransfer(Controller, Transfer) == OsSuccess) {
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
    if (Transfer->Transfer.Type == InterruptTransfer || Transfer->Transfer.Type == IsochronousTransfer) {
        UsbManagerIterateChain(Controller, Transfer->EndpointDescriptor, 
            USB_CHAIN_DEPTH, USB_REASON_RESET, HciProcessElement, Transfer);
        HciProcessEvent(Controller, USB_EVENT_RESTART_DONE, Transfer);

        // Don't notify driver when recieving a NAK response. Simply means device had
        // no data to send us. I just wished that it would leave the data intact instead.
        if (Transfer->Status != TransferNAK) {
            UsbManagerSendNotification(Transfer);
        }
        Transfer->Status    = TransferQueued;
        Transfer->Flags     = TransferFlagNone;
    }
    else if (Transfer->Transfer.Type == ControlTransfer || Transfer->Transfer.Type == BulkTransfer) {
        HciTransactionFinalize(Controller, Transfer, 0);
        if (!(Controller->Scheduler->Settings.Flags & USB_SCHEDULER_DEFERRED_CLEAN)) {
            Transfer->EndpointDescriptor = NULL;
            if (UsbManagerFinalizeTransfer(Controller, Transfer) == OsSuccess) {
                return ITERATOR_REMOVE;
            }
        }
    }
    return ITERATOR_CONTINUE;
}

/* UsbManagerProcessTransfers
 * Processes all the associated transfers with the given usb controller.
 * The iteration process will invoke <HciProcessElement> */
void
UsbManagerProcessTransfers(
    _In_ UsbManagerController_t*    Controller)
{
    // Simply invoke our iterator with a fixed shared implementation.
    UsbManagerIterateTransfers(Controller, UsbManagerProcessTransfer, NULL);
}

/* UsbManagerIterateChain
 * Iterates a given chain at the requested direction. The reason
 * for the iteration must also be provided to act accordingly. */
void
UsbManagerIterateChain(
    _In_ UsbManagerController_t*    Controller,
    _In_ uint8_t*                   ElementRoot,
    _In_ int                        Direction,
    _In_ int                        Reason,
    _In_ UsbSchedulerElementCallback ElementCallback,
    _In_ void*                      Context)
{
    // Variables
    UsbSchedulerObject_t *Object    = NULL;
    UsbSchedulerPool_t *Pool        = NULL;
    OsStatus_t Result               = OsSuccess;
    uint16_t RootIndex              = 0;
    uint16_t LinkIndex              = 0;
    uint8_t *Element                = ElementRoot;
    int Status                      = 0;
    
    // Debug
    assert(Controller != NULL);
    assert(ElementRoot != NULL);

    // Validate element and lookup pool
    Result                  = UsbSchedulerGetPoolFromElement(Controller->Scheduler, Element, &Pool);
    assert(Result == OsSuccess);
    Object                  = USB_ELEMENT_OBJECT(Pool, Element);
    
    // Get indices
    RootIndex               = Object->Index;
    LinkIndex               = (Direction == USB_CHAIN_BREATH) ? Object->BreathIndex : Object->DepthIndex;

    // Iterate to end, support cyclic queues
    while (Element) {
        // Support null-elements
        if (Controller->Scheduler->Settings.Flags & USB_SCHEDULER_NULL_ELEMENT) {
            if (LinkIndex == USB_ELEMENT_NO_INDEX) {
                break;
            }
        }

        Status          = ElementCallback(Controller, Element, Reason, Context);
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
        Pool            = USB_ELEMENT_GET_POOL(Controller->Scheduler, LinkIndex);
        Element         = USB_ELEMENT_INDEX(Pool, LinkIndex);
        Object          = USB_ELEMENT_OBJECT(Pool, Element);
        LinkIndex       = (Direction == USB_CHAIN_BREATH) ? Object->BreathIndex : Object->DepthIndex;
    }
}

/* UsbManagerDumpChain
 * Iterates a given chain at the requested direction. The function then
 * invokes the HciProcessElement with USB_REASON_DUMP. */
void
UsbManagerDumpChain(
    _In_ UsbManagerController_t*    Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ uint8_t*                   ElementRoot,
    _In_ int                        Direction)
{
    // Invoke HciProcessElement
    UsbManagerIterateChain(Controller, ElementRoot, 
        Direction, USB_REASON_DUMP, HciProcessElement, Transfer);
}
