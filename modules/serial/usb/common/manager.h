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

struct _UsbManagerTransfer {
	UUId_t					Requester;
	int						Pipe;
	void*					EndpointDescriptor;
};

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

/* UsbManagerCreateController
 * Registers a new controller with the usb-manager.
 * Identifies and registers with neccessary services */
__EXTERN
OsStatus_t
UsbManagerCreateController(
	_In_ void *Controller);

/* UsbManagerDestroyController
 * Unregisters a controller with the usb-manager.
 * Identifies and unregisters with neccessary services */
__EXTERN
OsStatus_t
UsbManagerDestroyController(
	_In_ void *Controller);

/* UsbManagerGetController 
 * Returns a controller by the given device-id */
__EXTERN
void*
UsbManagerGetController(
	_In_ UUId_t Device);

/* UsbManagerGetToggle 
 * Retrieves the toggle status for a given pipe */
__EXTERN
int
UsbManagerGetToggle(
	_In_ UUId_t Device,
	_In_ UUId_t Pipe);

/* UsbManagetSetToggle 
 * Updates the toggle status for a given pipe */
__EXTERN
void
UsbManagerSetToggle(
	_In_ UUId_t Device,
	_In_ UUId_t Pipe,
	_In_ int Toggle);

#endif //!_USB_MANAGER_H_
