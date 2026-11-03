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
 * - Shared controller interface for the USB drivers.
 */

#ifndef __USB_HCI__
#define __USB_HCI__

#include <os/osdefs.h>
#include "types.h"
#include "manager.h"

/**
 * @brief Create a USB controller from the bus device.
 *
 * @param busDevice The bus device describing the device hardware.
 * @return The controller, or NULL on failure.
 */
extern UsbManagerController_t*
HCIControllerCreate(
    _In_ Device_t* busDevice);

/**
 * @brief Stop and release resources allocated by HCIControllerCreate.
 *
 * @param controller The controller to destroy. It remains valid if cleanup fails.
 * @return OS_EOK after release, or the cleanup error.
 */
extern oserr_t
HCIControllerDestroy(
    _In_ UsbManagerController_t* controller);

/**
 * @brief Count controller elements for the supplied buffer.
 *
 * @param transfer Transfer metadata and, for DMA controllers, device segments.
 * @param transferLength Number of bytes in this submission.
 * @param direction Transfer direction.
 * @param elementCountOut Receives the required element count.
 * @return OS_EOK, or an error if the buffer layout is invalid.
 */
extern oserr_t
HCITransferElementsNeeded(
        _In_  UsbManagerTransfer_t*     transfer,
        _In_  uint32_t                  transferLength,
        _In_  enum USBTransferDirection direction,
        _Out_ int*                      elementCountOut);

extern void
HCITransferElementFill(
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ uint32_t                  transferLength,
        _In_ enum USBTransferDirection direction);

/**
 * @brief Reset a controller port.
 *
 * @param controller The controller to which the port belongs.
 * @param index The port index to reset.
 * @return OS_EOK if the port was reset, OS_EINVALPARAMS if the index is invalid,
 *         or OS_ETIMEOUT if the reset timed out.
 */
extern oserr_t
HCIPortReset(
    _In_ UsbManagerController_t* controller,
    _In_ int                     index);

/**
 * @brief Disable a root-hub port and confirm that it cannot respond at address zero.
 *
 * @param controller The controller. Its registers must still be accessible.
 * @param index A valid root-port index.
 * @return OS_EOK once the port is disabled, or a hardware error.
 */
extern oserr_t
HCIPortDisable(
    _In_ UsbManagerController_t* controller,
    _In_ int                     index);

/**
 * @brief Retrieve the current port status.
 *
 * @param controller The controller to which the port belongs.
 * @param index The port index.
 * @param port Receives the port status.
 */
extern void
HCIPortStatus(
        _In_ UsbManagerController_t* controller,
        _In_ int                     index,
        _In_ USBPortDescriptor_t*    port);

/**
 * @brief Reset the host controller's endpoint state so transfers can be queued again.
 * The class driver must first clear ENDPOINT_HALT on the device.
 *
 * @param controller The controller managing the endpoint.
 * @param address The address (hub/port/device/endpoint) identifying the endpoint.
 * @return OS_EOK if the reset was completed or queued.
 */
extern oserr_t
HCIEndpointReset(
    _In_ UsbManagerController_t* controller,
    _In_ USBAddress_t*           address);

/**
 * @brief Process a queue element for the common USB code.
 *
 * @param controller The controller to which the queue element belongs.
 * @param element The queue element to process.
 * @param reason The operation requested by the common USB code.
 * @param context The operation-specific context.
 * @return Whether to continue processing the chain.
 */
extern bool
HCIProcessElement(
        _In_ UsbManagerController_t* controller,
        _In_ uint8_t*                element,
        _In_ enum HCIProcessReason   reason,
        _In_ void*                   context);

/**
 * @brief Handle an event from the common USB code.
 *
 * @param controller The controller affected.
 * @param event The event the HCI code should handle.
 * @param context The event-specific context.
 */
extern void
HCIProcessEvent(
    _In_ UsbManagerController_t* controller,
    _In_ enum HCIProcessEvent    event,
    _In_ void*                   context);

/**
 * @brief Unlink a completed submission or release a transfer's controller resources.
 *
 * DMA controllers keep completed periodic descriptors for the next transfer.
 * These descriptors must be released during cancellation and cleanup.
 */
extern oserr_t
HCITransferFinalize(
        _In_ UsbManagerController_t* controller,
        _In_ UsbManagerTransfer_t*   transfer,
        _In_ bool                    deferredClean);

/**
 * @brief Fill and queue a non-isochronous transfer.
 * If controller resources are limited, the transfer may need several submissions.
 *
 * @param transfer The transfer that needs to be queued.
 * @return OS_EOK if the transfer has been queued.
 */
extern oserr_t
HCITransferQueue(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Queue an isochronous transfer without blocking.
 *
 * @param transfer The transfer to queue.
 */
extern oserr_t
HCITransferQueueIsochronous(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Cancel a queued transfer.
 *
 * @param transfer The transfer to cancel.
 */
extern oserr_t
HCITransferDequeue(
    _In_ UsbManagerTransfer_t* transfer);

/**
 * @brief Configure a USB hub.
 *
 * @param controller The controller managing the hub.
 * @param hubAddress The address of the hub to configure.
 * @param portCount The number of downstream ports on the hub.
 * @param characteristics The hub descriptor characteristics.
 * @return OS_EOK if the hub was configured, or an error code.
 */
extern oserr_t
HCIConfigureHub(
    _In_ UsbManagerController_t* controller,
    _In_ uint8_t                 hubAddress,
    _In_ uint8_t                 portCount,
    _In_ uint16_t                characteristics);

/**
 * @brief Detach a USB device from the controller.
 *
 * @param controller The controller managing the device.
 * @param address The address of the device to detach.
 * @return OS_EOK if the device was detached, or an error code.
 */
extern oserr_t
HCIDeviceDetach(
    _In_ UsbManagerController_t* controller,
    _In_ USBAddress_t*           address);

/**
 * @brief Handle an interrupt generated by an event descriptor.
 *
 * @param baseController The controller that the event descriptor belonged to.
 */
extern void
HciInterruptCallback(
    _In_ UsbManagerController_t* baseController);

/**
 * @brief Handle a timer event from the USB manager.
 * UHCI controllers need this to poll port status because they do not report hub events.
 *
 * @param baseController The controller that the event belonged to.
 */
extern void
HciTimerCallback(
    _In_ UsbManagerController_t* baseController);

#endif //!__USB_HCI__
