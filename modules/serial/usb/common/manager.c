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

/* Includes
 * - Library */
#include <stddef.h>
#include <stdlib.h>

/* Globals
 * Keeps track of the usb-manager state and its data */
static Collection_t *__GlbControllers   = NULL;
static UUId_t __GlbTransferId           = 0;
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
    _CRT_UNUSED(Timer);
    _CRT_UNUSED(Data);
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
    __GlbControllers    = CollectionCreate(KeyInteger);
    __GlbTransferId     = 0;
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
    _In_ int            IntervalMs,
    _In_ UsbCallback    Function)
{
    __GlbTimerEvent     = TimerStart(IntervalMs, 1, NULL);
    __GlbTimerCallback  = Function;
    return OsSuccess;
}

/* UsbManagerCreateTransfer
 * Creates a new transfer with the usb-manager.
 * Identifies and registers with neccessary services */
UsbManagerTransfer_t*
UsbManagerCreateTransfer(
    _In_ UsbTransfer_t* Transfer,
    _In_ UUId_t         Requester,
    _In_ int            ResponsePort,
    _In_ UUId_t         Device,
    _In_ UUId_t         Pipe)
{
    // Variables
    UsbManagerTransfer_t *UsbTransfer = NULL;

    // Allocate a new instance
    UsbTransfer                 = (UsbManagerTransfer_t*)malloc(sizeof(UsbManagerTransfer_t));
    memset(UsbTransfer, 0, sizeof(UsbManagerTransfer_t));

    // Copy information over
    memcpy(&UsbTransfer->Transfer, Transfer, sizeof(UsbTransfer_t));
    UsbTransfer->Requester      = Requester;
    UsbTransfer->ResponsePort   = ResponsePort;
    UsbTransfer->DeviceId       = Device;
    UsbTransfer->Pipe           = Pipe;
    UsbTransfer->Id             = __GlbTransferId++;
    return UsbTransfer;
}

/* UsbManagerCreateController
 * Registers a new controller with the usb-manager.
 * Identifies and registers with neccessary services */
OsStatus_t
UsbManagerCreateController(
    _In_ UsbManagerController_t *Controller)
{
    // Variables
    DataKey_t Key;
    Key.Value = (int)Controller->Device.Id;

    // Register controller with usbmanager service
    if (UsbControllerRegister(&Controller->Device, 
            Controller->Type, Controller->PortCount) != OsSuccess) {
        return OsError;
    }
    return CollectionAppend(__GlbControllers, CollectionCreateNode(Key, Controller));
}

/* UsbManagerDestroyController
 * Unregisters a controller with the usb-manager.
 * Identifies and unregisters with neccessary services */
OsStatus_t
UsbManagerDestroyController(
    _In_ UsbManagerController_t *Controller)
{
    // Variables
    CollectionItem_t *cNode = NULL;
    DataKey_t Key;

    // Unregister controller with usbmanager service
    Key.Value = (int)Controller->Device.Id;
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

/* UsbManagerGetController 
 * Returns a controller by the given device-id */
UsbManagerController_t*
UsbManagerGetController(
    _In_ UUId_t Device)
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
    _In_ UUId_t Device,
    _In_ UUId_t Pipe)
{
    // Variables
    DataKey_t Key;
    Key.Value = (int)Pipe;

    // Iterate list of controllers
    foreach(cNode, __GlbControllers) {
        // Cast data of node to our type
        UsbManagerController_t *Controller = (UsbManagerController_t*)cNode->Data;
        if (Controller->Device.Id == Device) {
            // Locate the correct endpoint
            foreach(eNode, Controller->Endpoints) {
                // Cast data again
                UsbManagerEndpoint_t *Endpoint =
                    (UsbManagerEndpoint_t*)eNode->Data;
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
            CollectionAppend(Controller->Endpoints, 
                CollectionCreateNode(Key, Endpoint));
        }
    }
    return 0;
}

/* UsbManagetSetToggle 
 * Updates the toggle status for a given pipe */
OsStatus_t
UsbManagerSetToggle(
    _In_ UUId_t Device,
    _In_ UUId_t Pipe,
    _In_ int Toggle)
{
    // Variables
    DataKey_t Key;
    Key.Value = (int)Pipe;

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

/* UsbManagerSendNotification 
 * Sends a notification to the subscribing process whenever a periodic
 * transaction has completed/failed. */
void
UsbManagerSendNotification(
    _In_ UsbManagerTransfer_t *Transfer)
{
    // If user doesn't want, ignore
    if (Transfer->Transfer.Flags & USB_TRANSFER_NO_NOTIFICATION) {
        return;
    }

    // Send interrupt
    InterruptDriver(
        Transfer->Requester,                        // Process
        (size_t)Transfer->Transfer.PeriodicData,    // Data pointer 
        Transfer->Status,                           // Status of transfer
        Transfer->CurrentDataIndex, 0);             // Data offset (not used in isoc)

    // Increase
    if (Transfer->Transfer.Type == InterruptTransfer) {
        Transfer->CurrentDataIndex = ADDLIMIT(0, Transfer->CurrentDataIndex,
            Transfer->Transfer.Transactions[0].Length, Transfer->Transfer.PeriodicBufferSize);
    }
}