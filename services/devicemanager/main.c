/* MollenOS
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
 * MollenOS MCore - Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */
//#define __TRACE

#include "devicemanager.h"
#include <os/driver.h>
#include <os/utils.h>
#include <ds/collection.h>
#include <bus.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Globals 
 * Keep track of all devices and contracts */
static Collection_t Contracts       = COLLECTION_INIT(KeyId);
static Collection_t Devices         = COLLECTION_INIT(KeyId);
static UUId_t DeviceIdGenerator     = 0;
static UUId_t ContractIdGenerator   = 0;

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t
OnLoad(void)
{
    // Register us with server manager
    if (RegisterService(__DEVICEMANAGER_TARGET) != OsSuccess) {
        return OsError; // Only one instance of devicemanager is allowed
    }
    return BusEnumerate();
}

/* OnUnload
 * This is called when the server is being unloaded
 * and should free all resources allocated by the system */
OsStatus_t
OnUnload(void)
{
    // N/A not supposed to happen
    return OsSuccess;
}

/* OnEvent
 * This is called when the server recieved an external evnet
 * and should handle the given event */
OsStatus_t
OnEvent(
    _In_ MRemoteCall_t *Message)
{
    // Debug
    TRACE("DeviceManager.OnEvent(Function %i)", Message->Function);

    // Which function is called?
    switch (Message->Function) {
        // Handles registration of a new device 
        // and store it with a custom version of
        // our own MCoreDevice
        case __DEVICEMANAGER_REGISTERDEVICE: {
            // Variables
            MCoreDevice_t *Device   = NULL;
            Flags_t DeviceFlags     = 0;
            UUId_t ParentDeviceId   = UUID_INVALID;
            UUId_t Result           = UUID_INVALID;
            
            // Extract variables
            ParentDeviceId  = (UUId_t)Message->Arguments[0].Data.Value;
            RPCCastArgumentToPointer(&Message->Arguments[1], (void**)&Device);
            DeviceFlags     = (Flags_t)Message->Arguments[2].Data.Value;

            // Sanitize buffer
            if (Device != NULL) {
                if (DmRegisterDevice(ParentDeviceId, Device, NULL, DeviceFlags, &Result) != OsSuccess) {
                    Result = UUID_INVALID;
                }
            }
            return RPCRespond(&Message->From, &Result, sizeof(UUId_t));
        } break;

        // Unregisters a device from the system, and 
        // signals all drivers that are attached to 
        // un-attach
        case __DEVICEMANAGER_UNREGISTERDEVICE: {
            WARNING("Got event __DEVICEMANAGER_UNREGISTERDEVICE");
        } break;

        // Queries device information and returns
        // information about the device and the drivers
        // attached
        case __DEVICEMANAGER_QUERYDEVICE: {

        } break;

        // What do?
        case __DEVICEMANAGER_IOCTLDEVICE: {
            // Extract argumenters
            MCoreDevice_t *Device   = NULL;
            OsStatus_t Result       = OsError;
            DataKey_t Key = { .Value.Id = Message->Arguments[0].Data.Value };
            Device      = CollectionGetDataByKey(&Devices, Key, 0);

            // Sanitizie
            if (Device != NULL) {
                if ((Message->Arguments[1].Data.Value & 0xFFFF) == __DEVICEMANAGER_IOCTL_BUS) {
                    Result = DmIoctlDevice(Device, Message->Arguments[2].Data.Value);
                }
                else if ((Message->Arguments[1].Data.Value & 0xFFFF) == __DEVICEMANAGER_IOCTL_EXT) {
                    Result = DmIoctlDeviceEx(Device, Message->Arguments[1].Data.Value,
                        Message->Arguments[2].Data.Value, Message->Arguments[3].Data.Value,
                        Message->Arguments[4].Data.Value);
                }
            }
            return RPCRespond(&Message->From, &Result, sizeof(OsStatus_t));
        } break;

        // Registers a driver for the given device 
        // We then store what contracts are related to 
        // which devices in order to keep track
        case __DEVICEMANAGER_REGISTERCONTRACT: {
            // Extract arguments
            MContract_t *Contract   = (MContract_t*)Message->Arguments[0].Data.Buffer;
            UUId_t Result           = UUID_INVALID;

            // Update sender in contract
            Contract->DriverId = Message->From.Process;

            // Evaluate request, but don't free
            // the allocated contract storage, we need it
            if (DmRegisterContract(Contract, &Result) != OsSuccess) {
                Result = UUID_INVALID;
            }
            return RPCRespond(&Message->From, &Result, sizeof(UUId_t));
        } break;

        // For now this function is un-implemented
        case __DEVICEMANAGER_UNREGISTERCONTRACT: {
            // Not Implemented
            WARNING("Got event __DEVICEMANAGER_UNREGISTERCONTRACT");
        } break;

        // Query a contract for information 
        // This usually redirects a message to
        // the corresponding driver
        case __DEVICEMANAGER_QUERYCONTRACT: {
            // Hold a response buffer
            void *ResponseBuffer = malloc(Message->Result.Length);
            void *NullPointer = NULL;

            // Query contract
            if (DmQueryContract((MContractType_t)Message->Arguments[0].Data.Value, 
                    (int)Message->Arguments[1].Data.Value,
                    (Message->Arguments[2].Type == ARGUMENT_REGISTER) ?
                    &Message->Arguments[2].Data.Value : Message->Arguments[2].Data.Buffer,
                    Message->Arguments[2].Length,
                    (Message->Arguments[3].Type == ARGUMENT_REGISTER) ?
                    &Message->Arguments[3].Data.Value : Message->Arguments[3].Data.Buffer,
                    Message->Arguments[3].Length,
                    (Message->Arguments[4].Type == ARGUMENT_REGISTER) ?
                    &Message->Arguments[4].Data.Value : Message->Arguments[4].Data.Buffer,
                    Message->Arguments[4].Length,
                    ResponseBuffer, Message->Result.Length) == OsSuccess) {
                RPCRespond(&Message->From, ResponseBuffer, Message->Result.Length);
            }
            else {
                RPCRespond(&Message->From, NullPointer, sizeof(void*));
            }

            // Cleanup
            free(ResponseBuffer);
        } break;

        default: {
        } break;
    }

    return OsSuccess;
}

/* DmRegisterDevice
 * Allows registering of a new device in the
 * device-manager, and automatically queries for a driver for the new device */
OsStatus_t
DmRegisterDevice(
	_In_  UUId_t                Parent,
	_In_  MCoreDevice_t*        Device, 
	_In_  const char*           Name,
	_In_  Flags_t               Flags,
	_Out_ UUId_t*               Id)
{
    MCoreDevice_t*  CopyDevice = NULL;
    DataKey_t       Key;

    // Argument checks
    _CRT_UNUSED(Parent);
    assert(Device != NULL);
    assert(Id != NULL);
    assert(Device->Length >= sizeof(MCoreDevice_t));

    // Update name 
    if (Name != NULL) {
        memcpy(&Device->Name[0], Name, strlen(Name));
    }

    // Debug
    TRACE("%u, Registered device %s, struct length %u", 
        GlbDeviceIdGen, &Device->Name[0], Device->Length);

    // Generate id and update out
    *Id = Device->Id    = DeviceIdGenerator++;
    Key.Value.Id        = Device->Id;

    // Allocate our own copy of the device
    CopyDevice          = (MCoreDevice_t*)malloc(Device->Length);
    memcpy(CopyDevice, Device, Device->Length);
    CollectionAppend(&Devices, CollectionCreateNode(Key, CopyDevice));

    // Now, we want to try to find a driver
    // for the new device
#ifndef __OSCONFIG_NODRIVERS
    if (Flags & __DEVICEMANAGER_REGISTER_LOADDRIVER) {
        TRACE("Loading driver by querying system.");
        return InstallDriver(CopyDevice, Device->Length);
    }
#endif
    return OsSuccess;
}

/* DmRegisterContract
 * Registers the given contact with the device-manager to let
 * the manager know we are handling this device, and what kind
 * of functionality the device supports */
OsStatus_t
DmRegisterContract(
    _In_  MContract_t*          Contract,
    _Out_ UUId_t*               Id)
{
    // Variables
    MContract_t *CopyContract = NULL;
    DataKey_t Key;

    // Argument checks
    assert(Contract != NULL);
    assert(Id != NULL);

    // Trace
    TRACE("Registered driver for device %u: %s", 
        Contract->DeviceId, &Contract->Name[0]);

    // Lookup device
    Key.Value.Id = Contract->DeviceId;
    if (CollectionGetDataByKey(&Devices, Key, 0) == NULL) {
        ERROR("Device id %u was not registered with the device manager",
            Contract->DeviceId);
        return OsError;
    }

    // Update contract id
    *Id                     = ContractIdGenerator++;
    Contract->ContractId    = *Id;
    Key.Value.Id            = *Id;

    // Allocate our own copy of the contract
    CopyContract = (MContract_t*)malloc(sizeof(MContract_t));
    memcpy(CopyContract, Contract, sizeof(MContract_t));
    return CollectionAppend(&Contracts, CollectionCreateNode(Key, CopyContract));
}

/* DmQueryContract
 * Handles the generic query function, by resolving
 * the correct driver and asking for data */
OsStatus_t 
DmQueryContract(
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
    // Iterate driver list and find a contract that
    // matches the request
    foreach(cNode, &Contracts) {
        MContract_t *Contract = (MContract_t*)cNode->Data;
        if (Contract->Type == Type) {
            return QueryDriver(Contract, Function, 
                Arg0, Length0, Arg1, Length1, Arg2, Length2,
                ResultBuffer, ResultLength);
        }
    }
    return OsError;
}
