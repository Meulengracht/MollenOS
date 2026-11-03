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
 * MollenOS Service - Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#ifndef __USBMANAGER_H__
#define __USBMANAGER_H__

#include <ddk/bufferpool.h>
#include <ds/list.h>
#include <ds/mstring.h>
#include <usb/usb.h>
#include <os/osdefs.h>

typedef struct UsbHub UsbHub_t;

/**
 * @brief USB context, descriptor fields and identity strings for a connected device.
 */
typedef struct UsbPortDevice {
    usb_device_context_t Base;

    uint8_t              ManufactorerIndex;
    uint8_t              ProductIndex;
    uint8_t              SerialIndex;

    mstring_t*           Manufacturer;
    mstring_t*           Product;
    mstring_t*           Serial;

    uint16_t             VendorId;
    uint16_t             ProductId;
    uint8_t              Class;
    uint8_t              Subclass;
    uint8_t              Protocol;
    uint8_t              DefaultConfiguration;
    uuid_t               DeviceId;
} UsbPortDevice_t;

typedef struct UsbPort {
    uint8_t          Address;
    uint8_t          Speed;
    int              Enabled;
    int              Connected;
    UsbPortDevice_t* Device;
} UsbPort_t;

/**
 * @brief Hub position in the USB topology, descriptor data and downstream ports.
 */
typedef struct UsbHub {
    uuid_t     ParentHubDeviceId; // Root hubs use their own device ID.
    uuid_t     ControllerDeviceId;
    uuid_t     DeviceId;
    uuid_t     DriverId;
    uint8_t    DeviceAddress;  // USB device address assigned to this hub
    uint8_t    PortAddress;    // Port on the parent hub where this hub is located
    size_t     PortCount;      // Number of downstream ports exposed by this hub
    uint16_t   Characteristics; // Hub descriptor characteristics used by the HCI
    UsbPort_t* Ports[USB_MAX_PORTS];
} UsbHub_t;

/**
 * @brief Port to disable or confirm disconnected before using address zero again.
 */
typedef struct UsbEnumerationRecovery {
    uuid_t  HubDeviceId;
    uuid_t  HubDriverId;
    uint8_t PortAddress;
    bool    Pending;
} UsbEnumerationRecovery_t;

/**
 * @brief Controller registration, USB address map and pending enumeration recovery.
 */
typedef struct UsbController {
    Device_t*                Device;
    element_t                Header;
    uuid_t                   DriverId;
    enum USBControllerKind   Type;
    UsbEnumerationRecovery_t EnumerationRecovery;
    uint32_t                 AddressMap[4]; // 128 bits for USB addresses 0 through 127.
} UsbController_t;

// Core topology routines must run in the topology worker. Initialize the core
// before protocol registration and finish cleanup only after joining the worker.
// Transfers run in the controller and class driver services.

/**
 * @brief Start the single topology worker before accepting RPCs.
 *
 * One caller must handle startup and shutdown. A previous shutdown must finish
 * before starting the worker again.
 *
 * @return OS_EOK, OS_EBUSY if already started, or OS_EOOM on job creation failure.
 */
__EXTERN oserr_t
UsbTopologyStart(void);

/**
 * @brief Stop accepting commands, drain the FIFO and join the worker.
 *
 * Must be called outside the worker while the service still processes replies.
 * The caller must serialize shutdown through final core cleanup. On failure,
 * the worker identifier and topology state stay valid so the caller can retry.
 *
 * @return OS_EOK when joined, OS_EBUSY for self-join, or OS_EUNKNOWN on join failure.
 */
__EXTERN oserr_t
UsbTopologyStop(void);

/** Initializes the USB manager state and USB library resources. */
__EXTERN oserr_t UsbCoreInitialize(void);

/**
 * @brief Drain topology work before freeing controller, hub and USB library state.
 *
 * Must be called by the caller handling startup and shutdown while the service
 * still processes replies.
 *
 * @return OS_EOK after cleanup. On failure, state stays intact for a later retry.
 */
__EXTERN oserr_t
UsbCoreDestroy(void);

/** Initializes the hub registry. Must run before hub registration. */
__EXTERN void UsbCoreHubsInitialize(void);

/** Releases all hubs and devices attached to registered controllers. */
__EXTERN void UsbCoreHubsCleanup(void);

/** Removes all registered controllers and their associated USB state. */
__EXTERN void UsbCoreControllersCleanup(void);

/**
 * Creates and enumerates a device connected to a hub port.
 * @param usbController Controller connected to the hub
 * @param usbHub        Hub containing the port
 * @param usbPort       Port where the device is connected
 * @return OS_EOK on successful enumeration, otherwise an error code
 */
__EXTERN oserr_t
UsbCoreDevicesCreate(
        _In_ UsbController_t* usbController,
        _In_ UsbHub_t*        usbHub,
        _In_ UsbPort_t*       usbPort);

/**
 * Disconnects and frees the device currently attached to a hub port.
 * @param controller Controller connected to the device
 * @param port       Port whose device should be destroyed
 * @return OS_EOK on success, otherwise an error code
 */
__EXTERN oserr_t
UsbCoreDevicesDestroy(
        _In_ UsbController_t* controller,
        _In_ UsbPort_t*       port);

/**
 *
 * Registers a hub and records its position in the USB topology. For an external
 * hub, the registration also provides the controller with the hub descriptor
 * characteristics needed to configure transaction translation.
 * @param parentHubDeviceId Device ID of the parent hub, or the controller for a root hub
 * @param hubDeviceId       Device ID of the hub being registered
 * @param hubDriverId       Driver ID that handles this hub's port operations
 * @param portCount         Number of downstream ports on the hub
 * @param characteristics   Raw hub descriptor characteristics
 * @return
 */
__EXTERN oserr_t
UsbCoreHubsRegister(
        _In_ uuid_t  parentHubDeviceId,
        _In_ uuid_t  hubDeviceId,
        _In_ uuid_t  hubDriverId,
        _In_ int     portCount,
        _In_ uint16_t characteristics);

/**
 * Removes a hub and destroys devices connected to its downstream ports.
 * @param hubDeviceId Device ID of the hub to unregister
 */
__EXTERN void
UsbCoreHubsUnregister(
        _In_ uuid_t hubDeviceId);

/**
 * Retrieves or creates the state object for a hub port.
 * @param hub       Hub containing the port
 * @param portIndex Hub-local port index
 * @return The port state, or NULL when the arguments are invalid or allocation fails
 */
__EXTERN UsbPort_t*
UsbCoreHubsGetPort(
        _In_ UsbHub_t* hub,
        _In_ uint8_t   portIndex);

/**
 * Looks up a registered hub by device ID.
 * @param hubDeviceId Device ID of the hub
 * @return The registered hub, or NULL when no matching hub exists
 */
__EXTERN UsbHub_t*
UsbCoreHubsGet(
        _In_ uuid_t hubDeviceId);

/**
 * Reserves an unused USB device address for a controller.
 * @param controller Controller whose address map should be updated
 * @param address    Receives the reserved address
 * @return OS_EOK on success, or OS_ENOENT when no address is available
 */
__EXTERN oserr_t
UsbCoreControllerReserveAddress(
        _In_  UsbController_t* controller,
        _Out_ int*             address);

/**
 * Releases a previously reserved USB device address.
 * @param controller Controller whose address map should be updated
 * @param address    Address to release. Address zero is never released.
 */
__EXTERN void
UsbCoreControllerReleaseAddress(
        _In_ UsbController_t* controller,
        _In_ int              address);

/**
 * Retrieves a registered controller by its device ID.
 * @param deviceId Device ID of the controller
 * @return The controller instance, or NULL when no matching controller exists
 */
__EXTERN UsbController_t*
UsbCoreControllerGet(
        _In_ uuid_t deviceId);

/**
 * @brief Register a controller and its root hub.
 *
 * The device and its identity strings must be allocated separately. After this
 * call, the caller must not access or free them, including when registration fails.
 *
 * @param driverId Controller driver handle.
 * @param device Converted device and its independently allocated identity strings.
 * @param controllerType USB controller kind.
 * @param rootPorts Number of root ports.
 * @return OS_EOK or an allocation/registration error.
 */
__EXTERN oserr_t
UsbCoreControllerRegister(
    _In_ uuid_t                 driverId,
    _In_ Device_t*              device,
    _In_ enum USBControllerKind controllerType,
    _In_ int                    rootPorts);

/**
 * @brief Remove a controller after recursively destroying its connected devices.
 * @param deviceId Controller device identifier.
 * @return OS_EOK or OS_EINVALPARAMS for an unknown controller.
 */
__EXTERN oserr_t
UsbCoreControllerUnregister(
    _In_ uuid_t deviceId);

/**
 * @brief Count registered controllers in the topology worker.
 * @return Registered controller count.
 */
__EXTERN int
UsbCoreControllerCount(void);

/**
 * @brief Copy controller query data. Must be called from the topology worker.
 * @param index Zero-based controller index.
 * @param snapshot Receives the controller data, or zeroes for an invalid index.
 */
__EXTERN void
UsbCoreControllerSnapshot(
    _In_  int                    index,
    _Out_ USBControllerDevice_t* snapshot);

/**
 * @brief Register a hub and configure its controller-side hub state.
 * @param parentHubDeviceId Parent hub identifier, or this hub for a root hub.
 * @param deviceId Hub device identifier.
 * @param driverId Hub driver handle.
 * @param portCount Number of downstream ports.
 * @param characteristics Hub descriptor characteristics.
 */
__EXTERN void
UsbCoreHubsRegisterAndConfigure(
    _In_ uuid_t   parentHubDeviceId,
    _In_ uuid_t   deviceId,
    _In_ uuid_t   driverId,
    _In_ int      portCount,
    _In_ uint16_t characteristics);

/**
 * @brief Process a connection change in the topology worker.
 * @param hubDeviceId Hub device identifier.
 * @param portAddress Hub-local port index.
 */
__EXTERN void
UsbCorePortEvent(
    _In_ uuid_t  hubDeviceId,
    _In_ uint8_t portAddress);

/**
 * @brief Process a port error in the topology worker.
 * @param hubDeviceId Hub device identifier.
 * @param portAddress Hub-local port index.
 */
__EXTERN void
UsbCorePortError(
    _In_ uuid_t  hubDeviceId,
    _In_ uint8_t portAddress);

#endif //!__USBMANAGER_H__
