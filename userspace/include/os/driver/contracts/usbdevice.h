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
 * MollenOS MCore - Contract Definitions & Structures (Usb-Device Contract)
 * - This header describes the base contract-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _CONTRACT_USBDEVICE_INTERFACE_H_
#define _CONTRACT_USBDEVICE_INTERFACE_H_

/* Includes 
 * - System */
#include <os/driver/usb/definitions.h>
#include <os/driver/driver.h>
#include <os/driver/device.h>
#include <os/driver/usb.h>
#include <os/osdefs.h>

/* MCoreUsbDevice_t
 * This is the base usb device structure definition
 * and is passed on to all usb-drivers on their initialization
 * to give them an overview and description of their device 
 * and functions to read/write directly to the device */
PACKED_TYPESTRUCT(MCoreUsbDevice, {
    MCoreDevice_t                   Base;
    UsbHcDevice_t                   Device;
    UsbHcInterface_t                Interface;
    UsbHcEndpointDescriptor_t       Endpoints[USB_MAX_ENDPOINTS];
    UUId_t                          DriverId;
    UUId_t                          DeviceId;
});

#endif
