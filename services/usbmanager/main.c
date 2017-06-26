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

/* Includes
 * - System */
#include <os/driver/usb.h>
#include "manager.h"

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t
OnLoad(void)
{
	// Initialize core-systems
	return UsbCoreInitialize();
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
	// Destroy core-systems and let them cleanup
	return UsbCoreDestroy();
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t
OnEvent(
	_In_ MRemoteCall_t *Message)
{
	// Variables
	OsStatus_t Result = OsSuccess;

	// Which function is called?
	switch (Message->Function) {
		case __USBMANAGER_REGISTERCONTROLLER: {
			// Register controller
			return UsbControllerRegister(Message->Sender, 
				(UUId_t)Message->Arguments[0].Data.Value, 
				(UsbControllerType_t)Message->Arguments[1].Data.Value,
				Message->Arguments[2].Data.Value);
		} break;

		case __USBMANAGER_UNREGISTERCONTROLLER: {
			// Unregister controller
			return UsbControllerUnregister(Message->Sender, 
				(UUId_t)Message->Arguments[0].Data.Value);
		} break;

		case __USBMANAGER_QUERYCONTROLLERS: {
			// Variables

			// Extract them from message

			// Query all existing controllers

			// Send response
		} break;

		case __USBMANAGER_PORTEVENT: {
			// Handle port event
			return UsbEventPort(Message->Sender, 
				(UUId_t)Message->Arguments[0].Data.Value, 
				(int)Message->Arguments[1].Data.Value);
		} break;

		// Don't handle anything else tbh
		default:
			break;
	}

	// Done
	return Result;
}
