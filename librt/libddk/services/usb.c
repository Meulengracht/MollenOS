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
 * Usb Definitions & Structures
 * - This file describes the usb-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/services/usb.h>
#include <os/ipc.h>

OsStatus_t
UsbControllerRegister(
    _In_ MCoreDevice_t*      Device,
    _In_ UsbControllerType_t Type,
    _In_ size_t              Ports)
{
	thrd_t       ServiceTarget = GetUsbService();
	IpcMessage_t Request;

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __USBMANAGER_REGISTERCONTROLLER);
	IpcSetTypedArgument(&Request, 1, Type);
	IpcSetTypedArgument(&Request, 2, Ports);
	IpcSetUntypedArgument(&Request, 0, Device, sizeof(MCoreDevice_t));
	
	return IpcInvoke(ServiceTarget, &Request,
		IPC_ASYNCHRONOUS | IPC_NO_RESPONSE, 0, NULL);
}

OsStatus_t
UsbControllerUnregister(
    _In_ UUId_t DeviceId)
{
	thrd_t       ServiceTarget = GetUsbService();
	IpcMessage_t Request;

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __USBMANAGER_UNREGISTERCONTROLLER);
	IpcSetTypedArgument(&Request, 1, DeviceId);
	
	return IpcInvoke(ServiceTarget, &Request,
		IPC_ASYNCHRONOUS | IPC_NO_RESPONSE, 0, NULL);
}

OsStatus_t
UsbEventPort(
    _In_ UUId_t     DeviceId,
    _In_ uint8_t    HubAddress,
    _In_ uint8_t    PortAddress)
{
	thrd_t       ServiceTarget = GetUsbService();
	IpcMessage_t Request;

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __USBMANAGER_PORTEVENT);
	IpcSetTypedArgument(&Request, 1, DeviceId);
	IpcSetTypedArgument(&Request, 2, HubAddress);
	IpcSetTypedArgument(&Request, 3, PortAddress);
	
	return IpcInvoke(ServiceTarget, &Request,
		IPC_ASYNCHRONOUS | IPC_NO_RESPONSE, 0, NULL);
}

OsStatus_t
UsbQueryControllerCount(
    _Out_ int* ControllerCount)
{
	thrd_t       ServiceTarget = GetDeviceService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!ControllerCount) {
	    return OsInvalidParameters;
	}

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __USBMANAGER_QUERYCONTROLLERCOUNT);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	*ControllerCount = *(int*)Result;
	return OsSuccess;
}

OsStatus_t
UsbQueryController(
    _In_ int                Index,
    _In_ UsbHcController_t* Controller)
{
	thrd_t       ServiceTarget = GetDeviceService();
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	if (!Controller) {
	    return OsInvalidParameters;
	}

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __USBMANAGER_QUERYCONTROLLER);
    IpcSetTypedArgument(&Request, 1, Index);
	
	Status = IpcInvoke(ServiceTarget, &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	memcpy(Controller, Result, sizeof(UsbHcController_t));
	return OsSuccess;
}
