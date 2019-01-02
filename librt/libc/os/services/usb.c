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
 * - This file describes the usb-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <os/contracts/usbhost.h>
#include <os/service.h>
#include <os/usb.h>

/* UsbQueryControllerCount
 * Queries the available number of usb controllers. */
OsStatus_t
UsbQueryControllerCount(
    _Out_ int* ControllerCount)
{
	MRemoteCall_t Request;

	RPCInitialize(&Request, __USBMANAGER_TARGET, 
        __USBMANAGER_INTERFACE_VERSION, __USBMANAGER_QUERYCONTROLLERCOUNT);
    RPCSetResult(&Request, (const void*)ControllerCount, sizeof(int));
	return RPCExecute(&Request);
}

/* UsbQueryController
 * Queries the controller with the given index. Index-max is
 * the controller count - 1. */
OsStatus_t
UsbQueryController(
    _In_ int                Index,
    _In_ UsbHcController_t* Controller)
{
	MRemoteCall_t Request;

	RPCInitialize(&Request, __USBMANAGER_TARGET, 
        __USBMANAGER_INTERFACE_VERSION, __USBMANAGER_QUERYCONTROLLER);
    RPCSetArgument(&Request, 0, (const void*)&Index, sizeof(int));
    RPCSetResult(&Request, (const void*)Controller, sizeof(UsbHcController_t));
	return RPCExecute(&Request);
}
