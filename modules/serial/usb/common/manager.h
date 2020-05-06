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

#ifndef _USB_MANAGER_H_
#define _USB_MANAGER_H_

#include <usb/usb.h>
#include <ddk/eventqueue.h>
#include <ds/collection.h>
#include <ddk/busdevice.h>
#include <os/spinlock.h>
#include "transfer.h"
#include "scheduler.h"

typedef struct _UsbManagerEndpoint {
    UUId_t Pipe;
    int    Toggle;
} UsbManagerEndpoint_t;

typedef struct _UsbManagerController {
    UUId_t              Id;
    UsbControllerType_t Type;
    BusDevice_t         Device;

    UUId_t              Interrupt;
    _Atomic(reg32_t)    InterruptStatus;
    size_t              PortCount;
    
    DeviceIo_t*         IoBase;
    UsbScheduler_t*     Scheduler;

    Collection_t*       Endpoints;
    Collection_t*       TransactionList;
    spinlock_t          Lock;
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

/* UsbManagerInitialize
 * Initializes the usb manager that keeps track of
 * all controllers and all attached devices */
__EXTERN OsStatus_t
UsbManagerInitialize(void);

/* UsbManagerDestroy
 * Cleans up the manager and releases resources allocated */
__EXTERN OsStatus_t
UsbManagerDestroy(void);

/* UsbManagerRegisterController
 * Registers the usb controller with the system. */
__EXTERN OsStatus_t
UsbManagerRegisterController(
    _In_ UsbManagerController_t* Controller);

/* UsbManagerDestroyController
 * Unregisters a controller with the usb-manager.
 * Identifies and unregisters with neccessary services */
__EXTERN OsStatus_t
UsbManagerDestroyController(
    _In_ UsbManagerController_t* Controller);

/* UsbManagerGetEventQueue
 * Retrieves the shared event queue that can be used for timed events. */
__EXTERN EventQueue_t*
UsbManagerGetEventQueue(void);

/* UsbManagerClearTransfers
 * Clears all queued transfers by iterating them and invoking Finalize.
 * This will also wake-up waiting processes and tell them it's off. */
__EXTERN void
UsbManagerClearTransfers(
    _In_ UsbManagerController_t* Controller);

/* UsbManagerIterateTransfers
 * Iterate the transfers associated with the given controller. The iteration
 * flow can be controlled with the return codes. */
__EXTERN void
UsbManagerIterateTransfers(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbTransferItemCallback ItemCallback,
    _In_ void*                   Context);

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

/* UsbManagerGetControllers
 * Retrieve a list of all attached controllers to the system. */
__EXTERN Collection_t*
UsbManagerGetControllers(void);

/* UsbManagerGetController 
 * Returns a controller by the given device-id */
__EXTERN UsbManagerController_t*
UsbManagerGetController(
    _In_ UUId_t Device);

/* UsbManagerGetToggle 
 * Retrieves the toggle status for a given pipe */
__EXTERN int
UsbManagerGetToggle(
    _In_ UUId_t          Device,
    _In_ UsbHcAddress_t* Address);

/* UsbManagetSetToggle 
 * Updates the toggle status for a given pipe */
__EXTERN OsStatus_t
UsbManagerSetToggle(
    _In_ UUId_t          Device,
    _In_ UsbHcAddress_t* Address,
    _In_ int             Toggle);

/* UsbManagerProcessTransfers
 * Processes all the associated transfers with the given usb controller.
 * The iteration process will invoke <HciProcessElement> */
__EXTERN void
UsbManagerProcessTransfers(
    _In_ UsbManagerController_t* Controller);

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
