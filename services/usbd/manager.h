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

#include <usb/usb.h>
#include <ddk/bufferpool.h>
#include <os/osdefs.h>
#include <ds/list.h>

typedef struct UsbHub UsbHub_t;

typedef struct UsbPortDevice {
    usb_device_context_t Base;
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

typedef struct UsbHub {
    uuid_t     ControllerDeviceId;
    uuid_t     DeviceId;
    uuid_t     DriverId;
    uint8_t    DeviceAddress;  // the usb device address of this hub
    uint8_t    PortAddress;    // the port 'index' this hub is located on
    size_t     PortCount;
    UsbPort_t* Ports[USB_MAX_PORTS];
} UsbHub_t;

typedef struct UsbController {
    Device_t            Device;
    element_t           Header;
    uuid_t              DriverId;
    UsbControllerType_t Type;
    uint32_t            AddressMap[4]; // 4 x 32 bits = 128 possible addresses which match the max in usb-spec
} UsbController_t;

__EXTERN oserr_t UsbCoreInitialize(void);
__EXTERN oserr_t UsbCoreDestroy(void);

__EXTERN void UsbCoreHubsInitialize(void);
__EXTERN void UsbCoreHubsCleanup(void);

__EXTERN void UsbCoreControllersCleanup(void);

/**
 *
 * @param usbController
 * @param usbHub
 * @param usbPort
 * @return
 */
__EXTERN oserr_t
UsbCoreDevicesCreate(
        _In_ UsbController_t* usbController,
        _In_ UsbHub_t*        usbHub,
        _In_ UsbPort_t*       usbPort);

/**
 *
 * @param controller
 * @param port
 * @return
 */
__EXTERN oserr_t
UsbCoreDevicesDestroy(
        _In_ UsbController_t* controller,
        _In_ UsbPort_t*       port);

/**
 *
 * @param parentHubDeviceId
 * @param hubDeviceId
 * @param hubDriverId
 * @param portCount
 * @return
 */
__EXTERN oserr_t
UsbCoreHubsRegister(
        _In_ uuid_t  parentHubDeviceId,
        _In_ uuid_t  hubDeviceId,
        _In_ uuid_t  hubDriverId,
        _In_ int     portCount);

/**
 *
 * @param hubDeviceId
 */
__EXTERN void
UsbCoreHubsUnregister(
        _In_ uuid_t hubDeviceId);

/**
 *
 * @param hub
 * @param portIndex
 * @return
 */
__EXTERN UsbPort_t*
UsbCoreHubsGetPort(
        _In_ UsbHub_t* hub,
        _In_ uint8_t   portIndex);

/**
 *
 * @param hubDeviceId
 * @return
 */
__EXTERN UsbHub_t*
UsbCoreHubsGet(
        _In_ uuid_t hubDeviceId);

/**
 * Reserves an device address for the specified controller
 * @param controller
 * @param address
 * @return
 */
__EXTERN oserr_t
UsbCoreControllerReserveAddress(
        _In_  UsbController_t* controller,
        _Out_ int*             address);

/**
 * Releases an previously allocated device address
 * @param controller
 * @param address
 */
__EXTERN void
UsbCoreControllerReleaseAddress(
        _In_ UsbController_t* controller,
        _In_ int              address);

/**
 * Retrieves an controller instance from a device id
 * @param deviceId
 * @return
 */
__EXTERN UsbController_t*
UsbCoreControllerGet(
        _In_ uuid_t deviceId);

#endif //!__USBMANAGER_H__
