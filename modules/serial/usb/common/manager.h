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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - USB Controller Manager
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */

#ifndef _USB_MANAGER_H_
#define _USB_MANAGER_H_

#include <ddk/busdevice.h>
#include <ds/hashtable.h>
#include <ds/list.h>
#include <os/spinlock.h>
#include <os/types/device.h>
#include <usb/usb.h>
#include "scheduler.h"
#include "transfer.h"

typedef struct UsbManagerController {
    uuid_t              Id;
    UsbControllerType_t Type;
    BusDevice_t*        Device;

    // IORequirements describes the required conformities that any
    // io-request made towards this controller must support. Conformity
    // are things like the memory-space width, and buffer alignment.
    struct OSIOCtlRequestRequirements IORequirements;

    int                 event_descriptor;
    uuid_t              Interrupt;
    _Atomic(reg32_t)    InterruptStatus;
    size_t              PortCount;
    
    DeviceIo_t*         IoBase;
    UsbScheduler_t*     Scheduler;

    hashtable_t Endpoints;
    list_t      TransactionList;
    spinlock_t  Lock;
} UsbManagerController_t;

#define USB_OUT_OF_RESOURCES       (void*)0
#define USB_INVALID_BUFFER         (void*)1

#define ITERATOR_CONTINUE           0
#define ITERATOR_STOP               (1 << 0)
#define ITERATOR_REMOVE             (1 << 1)

#define USB_EVENT_RESTART_DONE      0

#define USB_REASON_DUMP             0
#define USB_REASON_SCAN             1
#define USB_REASON_RESET            2
#define USB_REASON_FIXTOGGLE        3
#define USB_REASON_LINK             4
#define USB_REASON_UNLINK           5
#define USB_REASON_CLEANUP          6
typedef void(*UsbCallback)(void);
typedef int(*UsbTransferItemCallback)(
    _In_ UsbManagerController_t*    Controller,
    _In_ UsbManagerTransfer_t*      Transfer,
    _In_ void*                      Context);
typedef int(*UsbSchedulerElementCallback)(
    _In_ UsbManagerController_t*    Controller,
    _In_ uint8_t*                   Element,
    _In_ int                        Reason,
    _In_ void*                      Context);

/**
 * Initializes the common usb manager that all usb drivers can use to keep track of controllers
 * @return Status of the initialization.
 */
__EXTERN oserr_t
UsbManagerInitialize(void);

/**
 * Cleans up the common usb manager and destroys any controller registered.
 */
__EXTERN void
UsbManagerDestroy(void);

/**
 * Creates a new usb controller and registers it with the usb stack.
 * @param device        The physical device descriptor.
 * @param type          The type of the usb controller.
 * @param structureSize Size of the controller structure to allocate.
 * @return              A pointer to the newly allocated usb controller.
 */
__EXTERN UsbManagerController_t*
UsbManagerCreateController(
    _In_ BusDevice_t*        device,
    _In_ UsbControllerType_t type,
    _In_ size_t              structureSize);

/**
 * Registers the usb controller with the usb stack.
 * @param controller The controller that should be registered with the usb service.
 * @return           Status of the operation.
 */
__EXTERN oserr_t
UsbManagerRegisterController(
    _In_ UsbManagerController_t* controller);

/**
 * Destroys and unregisters the controller with the usb stack. Cleans up any resources allocated.
 * @param controller The controller that should be unregistered from the usb service and cleaned up.
 * @return           Status of the operation.
 */
__EXTERN oserr_t
UsbManagerDestroyController(
    _In_ UsbManagerController_t* controller);

/**
 * Clears all queued transfers by iterating them and invoking Finalize. This also notifies waiters.
 * @param controller The controller to clear transfers from.
 */
__EXTERN void
UsbManagerClearTransfers(
    _In_ UsbManagerController_t* controller);

/* UsbManagerIterateTransfers
 * Iterate the transfers associated with the given controller. The iteration
 * flow can be controlled with the return codes. */
__EXTERN void
UsbManagerIterateTransfers(
    _In_ UsbManagerController_t* controller,
    _In_ UsbTransferItemCallback itemCallback,
    _In_ void*                   context);

/* UsbManagerIterateChain
 * Iterates a given chain at the requested direction. The reason
 * for the iteration must also be provided to act accordingly. */
__EXTERN void
UsbManagerIterateChain(
    _In_ UsbManagerController_t*     Controller,
    _In_ uint8_t*                    ElementRoot,
    _In_ int                         Direction,
    _In_ int                         Reason,
    _In_ UsbSchedulerElementCallback ElementCallback,
    _In_ void*                       Context);

/* UsbManagerDumpChain
 * Iterates a given chain at the requested direction. The function then
 * invokes the HciProcessElement with USB_REASON_DUMP. */
__EXTERN void
UsbManagerDumpChain(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ uint8_t*                ElementRoot,
    _In_ int                     Direction);

/**
 * Get the controller by the given deviceId
 * @param deviceId The device deviceId of the controller.
 * @return         A pointer to the usb manager controller structure.
 */
__EXTERN UsbManagerController_t*
UsbManagerGetController(
        _In_ uuid_t deviceId);

/**
 * Gets the current toggle status of an endpoint address for the controller.
 * @param deviceId Device id of the controller.
 * @param address  Address of the endpoint.
 * @return         Toggle status.
 */
__EXTERN int
UsbManagerGetToggle(
        _In_ uuid_t          deviceId,
        _In_ UsbHcAddress_t* address);

/**
 * Updates the current toggle status of an endpoint address for the controller.
 * @param deviceId Device id of the controller.
 * @param address  Address of the endpoint.
 * @return         Status of the update operation.
 */
__EXTERN oserr_t
UsbManagerSetToggle(
        _In_ uuid_t          deviceId,
        _In_ UsbHcAddress_t* address,
        _In_ int             toggle);

/* UsbManagerProcessTransfers
 * Processes all the associated transfers with the given usb controller.
 * The iteration process will invoke <HciProcessElement> */
__EXTERN void
UsbManagerProcessTransfers(
    _In_ UsbManagerController_t* controller);

/* UsbManagerScheduleTransfers
 * Handles all transfers that are marked for either Schedule or Unscheduling.
 * The iteration process will invoke <HciProcessElement> */
__EXTERN void
UsbManagerScheduleTransfers(
    _In_ UsbManagerController_t* Controller);

/* UsbManagerDumpSchedule
 * Prints the entire schedule, frame for frame out. This fills a lot of space. */
__EXTERN void
UsbManagerDumpSchedule(
    _In_ UsbManagerController_t* Controller);

#endif //!_USB_MANAGER_H_
