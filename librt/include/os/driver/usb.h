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
 * MollenOS MCore - Usb Definitions & Structures
 * - This header describes the base usb-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef _USB_INTERFACE_H_
#define _USB_INTERFACE_H_

/* Includes
 * - Library */
#include <os/osdefs.h>

/* Includes
 * - System */
#include <os/driver/usb/definitions.h>
#include <os/driver/service.h>
#include <os/driver/buffer.h>
#include <os/ipc/ipc.h>

/* UsbControllerRegister
 * Registers a new controller with the given type and setup */
#ifdef __USBMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
UsbControllerRegister(
	_In_ void *Data,
	_In_ int Type,
	_In_ size_t Ports);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UsbControllerRegister(
	_In_ void *Data,
	_In_ int Type,
	_In_ size_t Ports)
{

}
#endif

/* UsbControllerUnregister
 * Unregisters the given usb-controller from the manager and
 * unregisters any devices registered by the controller */
#ifdef __USBMANAGER_IMPL
__EXTERN
OsStatus_t
SERVICEABI
UsbControllerUnregister(
	_In_ UUId_t Controller);
#else
SERVICEAPI
OsStatus_t
SERVICEABI
UsbControllerUnregister(
	_In_ UUId_t Controller)
{

}
#endif

#endif //!_USB_INTERFACE_H_
