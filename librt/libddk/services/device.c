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
 * Device (Protected) Definitions & Structures
 * - This file describes the device-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/contracts/base.h>
#include <ddk/services/service.h>
#include <ddk/device.h>
#include <os/ipc.h>
#include <string.h>

UUId_t
RegisterDevice(
    _In_    UUId_t          Parent,
    _InOut_ MCoreDevice_t*  Device, 
    _In_    Flags_t         Flags)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;

    // Protect against badly formed parameters
    if (Device == NULL || (Device->Length < sizeof(MCoreDevice_t))) {
        return UUID_INVALID;
    }
    
	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __DEVICEMANAGER_REGISTERDEVICE);
	IpcSetTypedArgument(&Request, 1, Parent);
	IpcSetTypedArgument(&Request, 2, Flags);
	IpcSetUntypedArgument(&Request, 0, Device, Device->Length);
	
	Status = IpcInvoke(GetDeviceService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return UUID_INVALID;
	}
	
	// Update id's
	Device->Id = *(UUId_t*)Result;
	return Device->Id;
}

OsStatus_t
UnregisterDevice(
    _In_ UUId_t DeviceId)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __DEVICEMANAGER_UNREGISTERDEVICE);
	IpcSetTypedArgument(&Request, 1, DeviceId);
	
	Status = IpcInvoke(GetDeviceService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
    return (OsStatus_t)*((int*)Result);
}

OsStatus_t
IoctlDevice(
    _In_ UUId_t  DeviceId,
    _In_ Flags_t Command,
    _In_ Flags_t Flags)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __DEVICEMANAGER_IOCTLDEVICE);
	IpcSetTypedArgument(&Request, 1, DeviceId);
	IpcSetTypedArgument(&Request, 2, Command);
	IpcSetTypedArgument(&Request, 3, Flags);
	
	Status = IpcInvoke(GetDeviceService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
    return (OsStatus_t)*((int*)Result);
}

OsStatus_t
IoctlDeviceEx(
    _In_    UUId_t   DeviceId,
    _In_    int      Direction,
    _In_    Flags_t  Register,
    _InOut_ Flags_t* Value,
    _In_    size_t   Width)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
    Flags_t      Select = 0;
    
    Select = __DEVICEMANAGER_IOCTL_EXT;
    if (Direction == 0) {
        Select |= __DEVICEMANAGER_IOCTL_EXT_READ;
    }

	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __DEVICEMANAGER_IOCTLDEVICE);
    IpcSetTypedArgument(&Request, 1, DeviceId);
    IpcSetTypedArgument(&Request, 2, Select);
    IpcSetTypedArgument(&Request, 3, Register);
    IpcSetTypedArgument(&Request, 4, (Value != NULL) ? *Value : 0);
    IpcSetUntypedArgument(&Request, 0, &Width, sizeof(size_t));
	
	Status = IpcInvoke(GetDeviceService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
	if (Direction == 0 && Value != NULL) {
        *Value = *(Flags_t*)Result;
    }
    return (OsStatus_t)(*(Flags_t*)Result);
}

OsStatus_t
RegisterContract(
    _In_ MContract_t* Contract)
{
	IpcMessage_t Request;
	OsStatus_t   Status;
	void*        Result;
	
	Contract->DriverId = thrd_current();
	
	IpcInitialize(&Request);
	IpcSetTypedArgument(&Request, 0, __DEVICEMANAGER_REGISTERCONTRACT);
	IpcSetUntypedArgument(&Request, 0, Contract, sizeof(MContract_t));
	
	Status = IpcInvoke(GetDeviceService(), &Request, 0, 0, &Result);
	if (Status != OsSuccess) {
	    return Status;
	}
	
    // Update the contract-id
    Contract->ContractId = *(UUId_t*)Result;
    return Status;
}
