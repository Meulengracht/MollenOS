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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */
#define __TRACE

#include <assert.h>
#include <bus.h>
#include <ctype.h>
#include "devicemanager.h"
#include <ddk/driver.h>
#include <ddk/utils.h>
#include <ds/collection.h>
#include <os/ipc.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

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
    thrd_t thr;
    
    *ServicePathOut = SERVICE_DEVICE_PATH;
    
    // Start the enumeration process in a new thread so we can quickly return
    // and be ready for requests.
    if (thrd_create(&thr, BusEnumerate, NULL) != thrd_success) {
        return OsError;
    }
    return OsSuccess;
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
    _In_ IpcMessage_t* Message)
{
    TRACE("DeviceManager.OnEvent(Function %i)", IPC_GET_TYPED(Message, 0));

    switch (IPC_GET_TYPED(Message, 0)) {
        case __DEVICEMANAGER_REGISTERDEVICE: {
            UUId_t         Result = UUID_INVALID;
            UUId_t         ParentDeviceId;
            MCoreDevice_t* Device;
            Flags_t        DeviceFlags;
            
            ParentDeviceId = (UUId_t)IPC_GET_TYPED(Message, 1);
            DeviceFlags    = (Flags_t)IPC_GET_TYPED(Message, 2);
            Device         = IPC_GET_UNTYPED(Message, 0);
            if (Device != NULL) {
                if (DmRegisterDevice(ParentDeviceId, Device, NULL, DeviceFlags, &Result) != OsSuccess) {
                    Result = UUID_INVALID;
                }
            }
            return IpcReply(Message, &Result, sizeof(UUId_t));
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
            DataKey_t      Key    = { .Value.Id = IPC_GET_TYPED(Message, 1) };
            
            Device = CollectionGetDataByKey(&Devices, Key, 0);
            if (Device != NULL) {
                if ((IPC_GET_TYPED(Message, 2) & 0xFFFF) == __DEVICEMANAGER_IOCTL_BUS) {
                    Result = DmIoctlDevice(Device, IPC_GET_TYPED(Message, 3));
                }
                else if ((IPC_GET_TYPED(Message, 2) & 0xFFFF) == __DEVICEMANAGER_IOCTL_EXT) {
                    Result = DmIoctlDeviceEx(Device, IPC_GET_TYPED(Message, 2),
                        IPC_GET_TYPED(Message, 3), IPC_GET_TYPED(Message, 4),
                        (size_t)IPC_GET_UNTYPED(Message, 0));
                }
            }
            return IpcReply(Message, &Result, sizeof(OsStatus_t));
        } break;

        // Registers a driver for the given device 
        // We then store what contracts are related to 
        // which devices in order to keep track
        case __DEVICEMANAGER_REGISTERCONTRACT: {
            MContract_t* Contract = (MContract_t*)IPC_GET_UNTYPED(Message, 0);
            UUId_t       Result   = UUID_INVALID;

            // Evaluate request, but don't free
            // the allocated contract storage, we need it
            if (DmRegisterContract(Contract, &Result) != OsSuccess) {
                Result = UUID_INVALID;
            }
            return IpcReply(Message, &Result, sizeof(UUId_t));
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

int
DmLoadDeviceDriver(void* Context)
{
    MCoreDevice_t* Device = Context;
    OsStatus_t     Status = InstallDriver(Device, Device->Length, NULL, 0);
    
    if (Status != OsSuccess) {
        return OsStatusToErrno(Status);
    }
    return 0;
}

OsStatus_t
DmRegisterDevice(
    _In_  UUId_t         Parent,
    _In_  MCoreDevice_t* Device, 
    _In_  const char*    Name,
    _In_  Flags_t        Flags,
    _Out_ UUId_t*        Id)
{
    MCoreDevice_t* CopyDevice;
    DataKey_t      Key = { 0 };

    _CRT_UNUSED(Parent);
    assert(Device != NULL);
    assert(Id != NULL);
    assert(Device->Length >= sizeof(MCoreDevice_t));

    CopyDevice = (MCoreDevice_t*)malloc(Device->Length);
    if (!CopyDevice) {
        return OsOutOfMemory;
    }
    
    memcpy(CopyDevice, Device, Device->Length);
    CopyDevice->Id = Key.Value.Id = DeviceIdGenerator++;
    if (Name != NULL) {
        memcpy(&CopyDevice->Name[0], Name, strlen(Name));
    }
    
    CollectionAppend(&Devices, CollectionCreateNode(Key, CopyDevice));
    TRACE("%u, Registered device %s, struct length %u", 
        DeviceIdGenerator, &CopyDevice->Name[0], CopyDevice->Length);
    *Id = CopyDevice->Id;
    
    // Now, we want to try to find a driver for the new device
#ifndef __OSCONFIG_NODRIVERS
    if (Flags & __DEVICEMANAGER_REGISTER_LOADDRIVER) {
        thrd_t thr;
        if (thrd_create(&thr, DmLoadDeviceDriver, CopyDevice) != thrd_success) {
            return OsError;
        }
    }
#endif
    return OsSuccess;
}

OsStatus_t
DmRegisterContract(
    _In_  MContract_t* Contract,
    _Out_ UUId_t*      Id)
{
    MContract_t* CopyContract;
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

    CopyContract = (MContract_t*)malloc(sizeof(MContract_t));
    if (!CopyContract) {
        return OsOutOfMemory;
    }
    
    memcpy(CopyContract, Contract, sizeof(MContract_t));
    CopyContract->ContractId = ContractIdGenerator++;
    Key.Value.Id             = *Id;
    
    *Id = CopyContract->ContractId;
    return CollectionAppend(&Contracts, CollectionCreateNode(Key, CopyContract));
}
