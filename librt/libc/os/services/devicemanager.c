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

#include <os/contracts/base.h>
#include <os/device.h>
#include <string.h>

/* Device Registering
 * Allows registering of a new device in the
 * device-manager, and automatically queries for a driver for the new device */
UUId_t
RegisterDevice(
    _In_    UUId_t          Parent,
    _InOut_ MCoreDevice_t*  Device, 
    _In_    Flags_t         Flags)
{
    // Variables
    MRemoteCall_t Request;
    UUId_t Result = UUID_INVALID;
    
    // Sanitize
    if (Device == NULL || (Device->Length < sizeof(MCoreDevice_t))) {
        return UUID_INVALID;
    }

    // Initialize RPC
    RPCInitialize(&Request, __DEVICEMANAGER_TARGET, 
        __DEVICEMANAGER_INTERFACE_VERSION, __DEVICEMANAGER_REGISTERDEVICE);
    RPCSetArgument(&Request, 0, (const void*)&Parent, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (const void*)Device, Device->Length);
    RPCSetArgument(&Request, 2, (const void*)&Flags, sizeof(Flags_t));
    
    // Result
    RPCSetResult(&Request, (const void*)&Result, sizeof(UUId_t));
    
    // Execute RPC
    if (RPCExecute(&Request) != OsSuccess) {
        return UUID_INVALID;
    }
    else {
        Device->Id = Result;
        return Result;
    }
}

/* Device Unregistering
 * Allows removal of a device in the device-manager, and automatically 
 * unloads drivers for the removed device */
OsStatus_t
UnregisterDevice(
    _In_ UUId_t DeviceId)
{
    // Variables
    MRemoteCall_t Request;
    OsStatus_t Result = OsSuccess;

    // Initialize RPC
    RPCInitialize(&Request, __DEVICEMANAGER_TARGET, 
        __DEVICEMANAGER_INTERFACE_VERSION, __DEVICEMANAGER_UNREGISTERDEVICE);
    RPCSetArgument(&Request, 0, (__CONST void*)&DeviceId, sizeof(UUId_t));
    RPCSetResult(&Request, (__CONST void*)&Result, sizeof(OsStatus_t));
    if (RPCExecute(&Request) != OsSuccess) {
        return OsError;
    }
    return Result;
}

/* Device I/O Control
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device */
OsStatus_t
IoctlDevice(
    _In_ UUId_t Device,
    _In_ Flags_t Command,
    _In_ Flags_t Flags)
{
    // Variables
    MRemoteCall_t Request;
    OsStatus_t Result = OsSuccess;

    // Initialize RPC
    RPCInitialize(&Request, __DEVICEMANAGER_TARGET, 
        __DEVICEMANAGER_INTERFACE_VERSION, __DEVICEMANAGER_IOCTLDEVICE);
    RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (__CONST void*)&Command, sizeof(Flags_t));
    RPCSetArgument(&Request, 2, (__CONST void*)&Flags, sizeof(Flags_t));
    RPCSetResult(&Request, (__CONST void*)&Result, sizeof(OsStatus_t));
    
    // Execute RPC
    RPCExecute(&Request);
    return Result;
}

/* Device I/O Control (Extended)
 * Allows manipulation of a given device to either disable
 * or enable, or configure the device.
 * <Direction> = 0 (Read), 1 (Write) */
OsStatus_t
IoctlDeviceEx(
    _In_ UUId_t Device,
    _In_ int Direction,
    _In_ Flags_t Register,
    _InOut_ Flags_t *Value,
    _In_ size_t Width)
{
    // Variables
    MRemoteCall_t Request;
    Flags_t Result = 0;
    Flags_t Select = 0;

    // Build selection
    Select = __DEVICEMANAGER_IOCTL_EXT;
    if (Direction == 0) {
        Select |= __DEVICEMANAGER_IOCTL_EXT_READ;
    }

    // Initialize RPC
    RPCInitialize(&Request, __DEVICEMANAGER_TARGET, 
        __DEVICEMANAGER_INTERFACE_VERSION, __DEVICEMANAGER_IOCTLDEVICE);
    RPCSetArgument(&Request, 0, (__CONST void*)&Device, sizeof(UUId_t));
    RPCSetArgument(&Request, 1, (__CONST void*)&Select, sizeof(Flags_t));
    RPCSetArgument(&Request, 2, (__CONST void*)&Register, sizeof(Flags_t));
    RPCSetArgument(&Request, 3, (__CONST void*)Value, sizeof(Flags_t));
    RPCSetArgument(&Request, 4, (__CONST void*)&Width, sizeof(size_t));
    RPCSetResult(&Request, (__CONST void*)&Result, sizeof(Flags_t));
    
    // Execute RPC
    RPCExecute(&Request);

    // Handle return
    if (Direction == 0 && Value != NULL) {
        *Value = Result;
    }
    else {
        return (OsStatus_t)Result;
    }

    // Read, discard value
    return OsSuccess;
}

/* RegisterContract 
 * Registers the given contact with the device-manager to let
 * the manager know we are handling this device, and what kind
 * of functionality the device supports */
OsStatus_t
RegisterContract(
    _In_ MContract_t *Contract)
{
    // Variables
    MRemoteCall_t Request;
    OsStatus_t Result = OsSuccess;
    UUId_t ContractId = UUID_INVALID;

    // Initialize static RPC variables like
    // type of RPC, pipe and version
    RPCInitialize(&Request, __DEVICEMANAGER_TARGET, 
        __DEVICEMANAGER_INTERFACE_VERSION, __DEVICEMANAGER_REGISTERCONTRACT);
    RPCSetArgument(&Request, 0, (const void*)Contract, sizeof(MContract_t));
    RPCSetResult(&Request, &ContractId, sizeof(UUId_t));
    Result = RPCExecute(&Request);

    // Update the contract-id
    Contract->ContractId = ContractId;
    return Result;
}

/* QueryContract 
 * Handles the generic query function, by resolving the correct driver and asking for data */
OsStatus_t
QueryContract(
    _In_      MContractType_t   Type, 
    _In_      int               Function,
    _In_Opt_  const void*       Arg0,
    _In_Opt_  size_t            Length0,
    _In_Opt_  const void*       Arg1,
    _In_Opt_  size_t            Length1,
    _In_Opt_  const void*       Arg2,
    _In_Opt_  size_t            Length2,
    _Out_Opt_ const void*       ResultBuffer,
    _In_Opt_  size_t            ResultLength)
{
    // Variables
    MRemoteCall_t Request;
    OsStatus_t Result = OsSuccess;

    // Initialize static RPC variables like
    // type of RPC, pipe and version
    RPCInitialize(&Request, __DEVICEMANAGER_TARGET, 
        __DEVICEMANAGER_INTERFACE_VERSION, __DEVICEMANAGER_QUERYCONTRACT);
    RPCSetArgument(&Request, 0, (const void*)&Type, sizeof(MContractType_t));
    RPCSetArgument(&Request, 1, (const void*)&Function, sizeof(int));

    // Handle arguments
    if (Arg0 != NULL && Length0 != 0) {
        RPCSetArgument(&Request, 2, Arg0, Length0);
    }
    if (Arg1 != NULL && Length1 != 0) {
        RPCSetArgument(&Request, 3, Arg1, Length1);
    }
    if (Arg2 != NULL && Length2 != 0) {
        RPCSetArgument(&Request, 4, Arg2, Length2);
    }

    // Handle result - if none is given we must always
    // get a osstatus - we also execute the rpc here
    if (ResultBuffer != NULL && ResultLength != 0) {
        RPCSetResult(&Request, ResultBuffer, ResultLength);
        return RPCExecute(&Request);
    }
    else {
        RPCSetResult(&Request, &Result, sizeof(OsStatus_t));
        if (RPCExecute(&Request) != OsSuccess) {
            return OsError;
        }
        else {
            return Result;
        }
    }
}
