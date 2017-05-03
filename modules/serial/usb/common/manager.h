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

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <ds/list.h>

/* UsbManagerInitialize
 * Initializes the usb manager that keeps track of
 * all controllers and all attached devices */
__EXTERN
OsStatus_t
UsbManagerInitialize(void);

/* UsbManagerDestroy
 * Cleans up the manager and releases resources allocated */
__EXTERN
OsStatus_t
UsbManagerDestroy(void);

/* UsbManagerCreateDevice
 * Registers a new device with the usb-manager on the specified
 * port and controller. Identifies and registers with neccessary services */
__EXTERN
OsStatus_t
UsbManagerCreateDevice(
	_In_ void *Controller,
	_In_ void *Port);

/* UsbManagerRemoveDevice
 * Removes an existing device from the usb-manager */
__EXTERN
OsStatus_t
UsbManagerRemoveDevice(
	_In_ void *Controller,
	_In_ void *Port);

/* UsbManagerGetDevice 
 * Retrieves device from the usb-id given */
__EXTERN
void*
UsbManagerGetDevice(
	_In_ UUId_t Id);

#endif //!_USB_MANAGER_H_
