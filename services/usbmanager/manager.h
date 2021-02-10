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
    UUId_t               DeviceId;
    UsbHub_t*            Hub;
} UsbPortDevice_t;

typedef struct UsbPort {
    uint8_t          Address;
    uint8_t          Speed;
    int              Enabled;
    int              Connected;
    UsbPortDevice_t* Device;
} UsbPort_t;

typedef struct UsbHub {
    uint8_t    Address;
    UsbPort_t* Ports[USB_MAX_PORTS];
} UsbHub_t;

typedef struct UsbController {
    Device_t            Device;
    element_t           Header;
    UUId_t              DriverId;
    UsbControllerType_t Type;
    size_t              PortCount;

    // Address Map 
    // 4 x 32 bits = 128 possible addresses
    // which match the max in usb-spec
    uint32_t AddressMap[4];
    UsbHub_t RootHub;
} UsbController_t;

/* UsbCoreInitialize
 * Initializes the usb-core stack driver. Allocates all neccessary resources
 * for managing usb controllers devices in the system. */
__EXTERN OsStatus_t
UsbCoreInitialize(void);

/* UsbCoreDestroy
 * Cleans up and frees any resouces allocated by the usb-core stack */
__EXTERN OsStatus_t
UsbCoreDestroy(void);

/* UsbCoreGetController 
 * Looks up the controller that matches the device-identifier */
__EXTERN UsbController_t*
UsbCoreGetController(
    _In_ UUId_t DeviceId);

#endif //!__USBMANAGER_H__
