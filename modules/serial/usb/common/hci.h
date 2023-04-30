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
 * MollenOS MCore - USB Controller Manager
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */

#ifndef __USB_HCI__
#define __USB_HCI__

#include <os/osdefs.h>
#include "types.h"
#include "manager.h"

/**
 * @brief Create a new usb controller instance from the given bus-device.
 * @param busDevice The bus device describing the device hardware.
 * @return A new instance of an usb controller if succesful.
 *         NULL on errors.
 */
extern UsbManagerController_t*
HCIControllerCreate(
    _In_ BusDevice_t* busDevice);

/**
 * @brief Frees any resources allocated by HCIControllerCreate.
 * @param controller The controller to cleanup.
 */
extern void
HCIControllerDestroy(
    _In_ UsbManagerController_t* controller);

extern oserr_t
HCITransferElementsNeeded(
        _In_  UsbManagerTransfer_t*     transfer,
        _In_  uint32_t                  transferLength,
        _In_  enum USBTransferDirection direction,
        _In_  SHMSGTable_t*             sgTable,
        _In_  uint32_t                  sgTableOffset,
        _Out_ int*                      elementCountOut);

extern void
HCITransferElementFill(
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ uint32_t                  transferLength,
        _In_ enum USBTransferDirection direction,
        _In_ SHMSGTable_t*             sgTable,
        _In_ uint32_t                  sgTableOffset);

/**
 * @brief Resets the port at the index.
 * @param controller The usb controller of which the port index belongs to.
 * @param index The port index that should be reset
 * @return OS_EINVALPARAMS if the index was invalid.
 *         OS_ETIMEOUT if the port could not reset within the timeout.
 *         OS_EOK if the port was succesfully reset.
 */
extern oserr_t
HCIPortReset(
    _In_ UsbManagerController_t* controller,
    _In_ int                     index);

/**
 * @brief Retrieve the current port status.
 * @param controller The controller associated with the port index
 * @param index The port index of the controller
 * @param port The port status descriptor that should be updated.
 */
extern void
HCIPortStatus(
        _In_ UsbManagerController_t* controller,
        _In_ int                     index,
        _In_ USBPortDescriptor_t*    port);

/**
 * @brief Invoked by the USB common code every time queue elements
 * needs to be updated or on certain events.
 * @param controller The controller instance of the queue element.
 * @param element The queue element it needs to be updated/changed.
 * @param reason  The expected action from the usb common code.
 * @param context The context will vary based on the reason code.
 * @return 
 */
extern bool
HCIProcessElement(
        _In_ UsbManagerController_t* controller,
        _In_ uint8_t*                element,
        _In_ enum HCIProcessReason   reason,
        _In_ void*                   context);

/**
 * @brief Called by the common usb code to indicate an event has
 * passed. The context can vary by event type.
 * @param controller The controller affected.
 * @param event The event the HCI code should handle.
 * @param context The context associated with the event.
 */
extern void
HCIProcessEvent(
    _In_ UsbManagerController_t* controller,
    _In_ enum HCIProcessEvent    event,
    _In_ void*                   context);

/**
 * @brief Finalizes a transfer by unlinking and maybe also cleaning any resources allocated.
 */
extern oserr_t
HCITransferFinalize(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer,
        _In_ bool                    deferredClean);

/**
 * @brief Fills and queues a non-isoc transfer. No guarantee is made towards the transfer
 * can be completed at once. Transfer success may depend on resource allocation, and a partial
 * transfer may be done.
 * @param transfer The transfer that needs to be queued.
 * @return OS_EOK if the transfer has been queued.
 */
extern oserr_t
HCITransferQueue(
    _In_ UsbManagerTransfer_t* transfer);

/* HCITransferQueueIsochronous
 * Queues a new isochronous transfer for the given driver and pipe. 
 * The function does not block. */
extern oserr_t
HCITransferQueueIsochronous(
    _In_ UsbManagerTransfer_t* transfer);

/* HCITransferDequeue
 * Removes a queued transfer from the controller's transfer list */
extern oserr_t
HCITransferDequeue(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * Called when an interrupt was generated by an event descriptor
 * @param baseController The controller that the event descriptor belonged to.
 */
extern void
HciInterruptCallback(
    _In_ UsbManagerController_t* baseController);

/**
 * @brief Called when an timer event was generated by the usb manager.
 * This is primarily targetted at UHCI controllers where hub events
 * are not supported, so we manually must query port status.
 * @param baseController The controller that the event belonged to.
 */
extern void
HciTimerCallback(
    _In_ UsbManagerController_t* baseController);

#endif //!__USB_HCI__
