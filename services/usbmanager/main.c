/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#include <ddk/services/usb.h>
#include <ddk/utils.h>
#include <os/ipc.h>
#include "manager.h"

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t
OnLoad(
    _In_ char** ServicePathOut)
{
	*ServicePathOut = SERVICE_USB_PATH;
	return UsbCoreInitialize();
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
	return UsbCoreDestroy();
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event*/
OsStatus_t
OnEvent(
    _In_ IpcMessage_t* Message)
{
	OsStatus_t Result = OsSuccess;

	// Which function is called?
	switch (IPC_GET_TYPED(Message, 0)) {
		case __USBMANAGER_REGISTERCONTROLLER: {
			// Register controller
			return UsbCoreControllerRegister(Message->Sender, 
				(MCoreDevice_t*)IPC_GET_UNTYPED(Message, 0), 
				(UsbControllerType_t)IPC_GET_TYPED(Message, 1),
				IPC_GET_TYPED(Message, 2));
		} break;

		case __USBMANAGER_UNREGISTERCONTROLLER: {
			// Unregister controller
			return UsbCoreControllerUnregister(Message->Sender, 
				(UUId_t)IPC_GET_TYPED(Message, 1));
		} break;

		case __USBMANAGER_QUERYCONTROLLERCOUNT: {
            int ControllerCount = UsbCoreGetControllerCount();
            return IpcReply(Message, &ControllerCount, sizeof(int));
		} break;

        case __USBMANAGER_QUERYCONTROLLER: {
            UsbHcController_t HcController = { { 0 }, 0 };
            UsbController_t*  Controller   = NULL;
            Controller                     = UsbCoreGetControllerIndex((int)IPC_GET_TYPED(Message, 1));
            
            if (Controller != NULL) {
                memcpy(&HcController.Device, &Controller->Device, sizeof(MCoreDevice_t));
                HcController.Type = Controller->Type;
            }
            return IpcReply(Message, &HcController, sizeof(UsbHcController_t));
        } break;

		case __USBMANAGER_PORTEVENT: {
			return UsbCoreEventPort(Message->Sender, 
				(UUId_t)IPC_GET_TYPED(Message, 1), 
				LOBYTE(IPC_GET_TYPED(Message, 2)), 
				LOBYTE(IPC_GET_TYPED(Message, 3)));
		} break;

		default:
			break;
	}
	return Result;
}
