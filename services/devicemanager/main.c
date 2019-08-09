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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */
#define __TRACE

#include "devicemanager.h"
#include <ddk/driver.h>
#include <ddk/utils.h>
#include <ddk/service.h>
#include <ds/collection.h>
#include <bus.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static Collection_t Contracts           = COLLECTION_INIT(KeyId);
static Collection_t Devices             = COLLECTION_INIT(KeyId);
static UUId_t       DeviceIdGenerator   = 0;
static UUId_t       ContractIdGenerator = 0;

/* OnLoad
 * The entry-point of a server, this is called
 * as soon as the server is loaded in the system */
OsStatus_t
OnLoad(
    _In_ char** ServicePathOut)
{
    *ServicePathOut = SERVICE_DEVICE_PATH;
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
        case __DEVICEMANAGER_REGISTERDEVICE: {
            UUId_t         Result = UUID_INVALID;
            UUId_t         ParentDeviceId;
            MCoreDevice_t* Device;
            Flags_t        DeviceFlags;
            
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
            WARNING("Got event __DEVICEMANAGER_QUERYDEVICE");
        } break;

        // What do?
        case __DEVICEMANAGER_IOCTLDEVICE: {
            MCoreDevice_t* Device;
            OsStatus_t     Result = OsError;
            DataKey_t      Key    = { .Value.Id = Message->Arguments[0].Data.Value };
            
            Device = CollectionGetDataByKey(&Devices, Key, 0);
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
            MContract_t* Contract = (MContract_t*)Message->Arguments[0].Data.Buffer;
            UUId_t       Result   = UUID_INVALID;

            // Evaluate request, but don't free
            // the allocated contract storage, we need it
            Contract->DriverId = Message->From.Process;
            if (DmRegisterContract(Contract, &Result) != OsSuccess) {
                Result = UUID_INVALID;
            }
            return RPCRespond(&Message->From, &Result, sizeof(UUId_t));
        } break;

        // For now this function is un-implemented
        case __DEVICEMANAGER_UNREGISTERCONTRACT: {
            WARNING("Got event __DEVICEMANAGER_UNREGISTERCONTRACT");
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
    MCoreDevice_t* CopyDevice;
    DataKey_t      Key = { 0 };

    // Argument checks
    _CRT_UNUSED(Parent);
    assert(Device != NULL);
    assert(Id != NULL);
    assert(Device->Length >= sizeof(MCoreDevice_t));

    // Update name 
    if (Name != NULL) {
        memcpy(&Device->Name[0], Name, strlen(Name)); // skip null term
    }
    TRACE("%u, Registered device %s, struct length %u", 
        DeviceIdGenerator, &Device->Name[0], Device->Length);

    // Generate id and update out
    *Id = Device->Id = DeviceIdGenerator++;
    Key.Value.Id     = Device->Id;

    // Allocate our own copy of the device
    CopyDevice = (MCoreDevice_t*)malloc(Device->Length);
    if (!CopyDevice) {
        return OsOutOfMemory;
    }
    
    memcpy(CopyDevice, Device, Device->Length);
    CollectionAppend(&Devices, CollectionCreateNode(Key, CopyDevice));

    // Now, we want to try to find a driver for the new device
#ifndef __OSCONFIG_NODRIVERS
    if (Flags & __DEVICEMANAGER_REGISTER_LOADDRIVER) {
        return InstallDriver(CopyDevice, Device->Length, NULL, 0);
    }
#endif
    return OsSuccess;
}

OsStatus_t
DmRegisterContract(
    _In_  MContract_t* Contract,
    _Out_ UUId_t*      Id)
{
    MContract_t* CopyContract = NULL;
    DataKey_t    Key;

    assert(Contract != NULL);
    assert(Id != NULL);

    TRACE("%u: Driver installed for %u: %s", Contract->DriverId, Contract->DeviceId, &Contract->Name[0]);

    // Lookup device
    Key.Value.Id = Contract->DeviceId;
    if (CollectionGetDataByKey(&Devices, Key, 0) == NULL) {
        ERROR("Device id %u was not registered with the device manager",
            Contract->DeviceId);
        return OsError;
    }

    // Update contract id
    *Id                  = ContractIdGenerator++;
    Contract->ContractId = *Id;
    Key.Value.Id         = *Id;

    // Allocate our own copy of the contract
    CopyContract = (MContract_t*)malloc(sizeof(MContract_t));
    if (!CopyContract) {
        return OsOutOfMemory;
    }
    memcpy(CopyContract, Contract, sizeof(MContract_t));
    return CollectionAppend(&Contracts, CollectionCreateNode(Key, CopyContract));
}
