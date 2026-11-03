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
 * - Shared controller state and transfer handling for USB drivers.
 */

#ifndef _USB_MANAGER_H_
#define _USB_MANAGER_H_

#include <ddk/devices/bus.h>
#include <ddk/interrupts/interrupt.h>
#include <ds/hashtable.h>
#include <ds/list.h>
#include <os/spinlock.h>
#include <os/types/device.h>
#include <usb/usb.h>
#include "types.h"
#include "scheduler.h"
#include "transfer.h"

enum UsbManagerControllerPhase {
    USB_CONTROLLER_INITIALIZING,
    USB_CONTROLLER_READY,
    USB_CONTROLLER_DESTROY_PENDING,
    USB_CONTROLLER_DESTROYING
};

/**
 * @brief Controller setup and cleanup state, active operations and DMA status.
 */
typedef struct UsbManagerControllerState {
    enum UsbManagerControllerPhase Phase;
    unsigned int                   OperationsInFlight;
    bool                           ProcessingTransfers;
    bool                           DmaStopped;
} UsbManagerControllerState_t;

/**
 * @brief Common controller resources, endpoint toggles and transfer queues.
 */
typedef struct UsbManagerController {
    uuid_t                           Id;
    enum USBControllerKind           Kind;
    Device_t*                        Device;
    struct DMAContext*               DmaContext;
    struct UsbManagerControllerState State;

    // I/O buffers must meet the controller's address width and alignment requirements.
    struct OSIOCtlRequestRequirements IORequirements;

    int                 event_descriptor;
    DeviceInterrupt_t   Interrupt;
    _Atomic(reg32_t)    InterruptStatus;
    size_t              PortCount;
    
    DeviceIo_t*         IoBase;
    UsbScheduler_t*     Scheduler;

    hashtable_t  Endpoints;
    list_t       TransactionList;
    list_t       ReadyTransfers;
    list_t       WaitingTransfers;
    element_t    DestroyElement;
    spinlock_t   Lock;
} UsbManagerController_t;

#define USB_OUT_OF_RESOURCES       (void*)0
#define USB_INVALID_BUFFER         (void*)1

typedef int(*UsbTransferItemCallback)(UsbManagerController_t*, UsbManagerTransfer_t*, void*);
typedef bool(*UsbSchedulerElementCallback)(UsbManagerController_t*, uint8_t*, enum HCIProcessReason, void*);

/**
 * @brief Initialize the shared USB controller manager.
 *
 * @return Status of the initialization.
 */
extern oserr_t
UsbManagerInitialize(void);

/**
 * @brief Clean up the USB manager and destroy registered controllers.
 */
extern void
UsbManagerDestroy(void);

/**
 * @brief Allocate controller storage and initialize the common controller state.
 *
 * @param device        The physical device descriptor.
 * @param kind          USB controller type.
 * @param structureSize Size of the controller structure to allocate.
 * @return              The controller, or NULL if allocation or setup fails.
 */
extern UsbManagerController_t*
UsbManagerCreateController(
    _In_ Device_t*              device,
    _In_ enum USBControllerKind kind,
    _In_ size_t                 structureSize);

/**
 * @brief Register the controller with the USB service.
 *
 * @param controller Controller to register.
 * @return           Status of the operation.
 */
extern oserr_t
UsbManagerRegisterController(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Mark the controller ready after hardware setup succeeds.
 * Setup can yield after interrupts are enabled. If removal was requested during
 * setup, the caller must follow its error cleanup path.
 *
 * @param controller Controller with hardware setup complete.
 * @return true if INITIALIZING changed to READY, false if the state already changed.
 */
extern bool
UsbManagerCompleteControllerInitialization(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Remove common controller state after the HCI resources are released.
 * Requires DESTROYING with no operations in flight. New operations remain blocked.
 * The HCI caller must free the controller storage after this call.
 *
 * @param controller Controller whose hardware and DMA resources are released.
 */
extern void
UsbManagerDestroyController(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Queue the controller for cleanup by the monitor.
 * Requires calls to run on the service event loop. Repeated calls leave the
 * existing queue entry in place.
 *
 * @param controller Controller to clean up. Setup may still be incomplete.
 */
extern void
UsbManagerDeferControllerDestroy(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Block new operations and begin cleanup when active operations finish.
 * Requires controller service jobs to run serially. The controller stays on the
 * retry list if cleanup fails. Recursive calls cannot begin a second cleanup.
 *
 * @param controller Controller to clean up. Setup may still be incomplete.
 * @return true if cleanup may proceed, false if an operation or cleanup is active.
 */
extern bool
UsbManagerBeginControllerDestroy(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Leave failed cleanup queued for the next monitor tick.
 * Requires UsbManagerBeginControllerDestroy to have succeeded and the controller
 * to remain in the manager. New operations stay blocked. Cleared resource fields
 * show which resources have already been released.
 *
 * @param controller Controller whose cleanup failed.
 */
extern void
UsbManagerRetryControllerDestroy(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Set up the common interrupt resources for a USB host controller.
 * Interrupts stay disabled so the caller can add resources, such as the OHCI
 * communication area, before calling DeviceInterruptEnable. On error, the caller
 * must destroy Base.Interrupt before freeing the controller storage.
 *
 * @param controller     Controller to configure.
 * @param handler        Controller interrupt handler.
 * @param controllerSize Size of the controller structure used by the handler.
 * @return               Status of the interrupt setup.
 */
extern oserr_t
UsbManagerConfigureInterrupt(
    _In_ UsbManagerController_t* controller,
    _In_ InterruptHandler_t      handler,
    _In_ size_t                  controllerSize);

/**
 * @brief Finalize queued transfers and notify waiters.
 *
 * @param controller The controller to clear transfers from.
 */
extern void
UsbManagerClearTransfers(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Call the callback for each transfer on the controller.
 * The callback return flags control removal and when iteration stops.
 *
 * @param controller   Controller to iterate.
 * @param itemCallback Callback for each transfer.
 * @param context      Context passed to the callback.
 */
extern void
UsbManagerIterateTransfers(
    _In_ UsbManagerController_t* controller,
    _In_ UsbTransferItemCallback itemCallback,
    _In_ void*                   context);

/**
 * @brief Walk the descriptor chain in the requested direction.
 *
 * @param Controller      Controller containing the chain.
 * @param ElementRoot     First element in the chain.
 * @param Direction       Chain direction to follow.
 * @param Reason          Operation passed to the callback.
 * @param ElementCallback Callback for each element. Return false to stop.
 * @param Context         Context passed to the callback.
 */
extern void
UsbManagerChainEnumerate(
    _In_ UsbManagerController_t*     Controller,
    _In_ uint8_t*                    ElementRoot,
    _In_ int                         Direction,
    _In_ int                         Reason,
    _In_ UsbSchedulerElementCallback ElementCallback,
    _In_ void*                       Context);

/**
 * @brief Dump the chain through HCIProcessElement with HCIPROCESS_REASON_DUMP.
 *
 * @param Controller  Controller containing the chain.
 * @param Transfer    Transfer passed to HCIProcessElement.
 * @param ElementRoot First element in the chain.
 * @param Direction   Chain direction to follow.
 */
extern void
UsbManagerDumpChain(
    _In_ UsbManagerController_t* Controller,
    _In_ UsbManagerTransfer_t*   Transfer,
    _In_ uint8_t*                ElementRoot,
    _In_ int                     Direction);

/**
 * @brief Find a controller by its device ID.
 *
 * @param deviceId Device ID of the controller.
 * @return         The controller, or NULL if it is not registered.
 */
extern UsbManagerController_t*
UsbManagerGetController(
        _In_ uuid_t deviceId);

/**
 * @brief Endpoint data toggles.
 * The toggle sequence depends on the endpoint type.
 * For control transfers
 * | SETUP(0) | DATA(1) | DATA(0) | ... | STATUS(1)
 * For bulk transfers
 * | DATA(0) | DATA(1) | .....
 */

/**
 * @brief Get the current data toggle for an endpoint.
 *
 * @param controller Controller handling the endpoint.
 * @param address    Address of the endpoint.
 * @return           Current toggle, or zero if no toggle has been recorded.
 */
extern int
UsbManagerGetToggle(
        _In_ UsbManagerController_t* controller,
        _In_ USBAddress_t*           address);

/**
 * @brief Set the current data toggle for an endpoint.
 *
 * @param controller Controller handling the endpoint.
 * @param address    Address of the endpoint.
 * @param toggle     New toggle value.
 */
extern void
UsbManagerSetToggle(
        _In_ UsbManagerController_t* controller,
        _In_ USBAddress_t*           address,
        _In_ int                     toggle);

/**
 * @brief Process queued work for xHCI, or scan transfers for legacy controllers.
 * Each queued work item gets one attempt per dispatch. Failed cleanup or
 * synchronization stays queued for retry. The xHCI path does not scan active transfers.
 *
 * @param controller Controller whose service event loop is dispatching work.
 */
extern void
UsbManagerProcessTransfers(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Queue completion or cleanup work once, without scanning active transfers.
 * Requires calls and processing to run on the controller's service event loop.
 *
 * @param controller Controller handling the transfer.
 * @param transfer   Transfer that must remain valid until work is processed or removed.
 */
extern void
UsbManagerQueueTransferWork(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer);

/**
 * @brief Wait for a controller command before submitting the transfer.
 *
 * @param controller Controller handling the transfer.
 * @param transfer   Transfer to mark waiting and add to the wait queue.
 */
extern void
UsbManagerWaitTransfer(
    _In_ UsbManagerController_t* controller,
    _In_ UsbManagerTransfer_t*   transfer);

/**
 * @brief Queue waiting transfers for another submission attempt.
 *
 * @param controller Controller whose command completed.
 */
extern void
UsbManagerWakeTransfers(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Unlink queued work before destroying or processing a transfer.
 *
 * @param transfer Transfer whose queue entry must be unlinked.
 */
extern void
UsbManagerForgetTransferWork(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Schedule or unschedule marked transfers through HCIProcessElement.
 *
 * @param controller Controller whose transfers need schedule updates.
 */
extern void
UsbManagerScheduleTransfers(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Print the full schedule, one frame at a time.
 *
 * @param Controller Controller whose schedule to print.
 */
extern void
UsbManagerDumpSchedule(
    _In_ UsbManagerController_t* Controller);

#endif //!_USB_MANAGER_H_
