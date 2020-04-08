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
#include "svc_device_protocol_server.h"
#include <ddk/utils.h>
#include <ds/collection.h>
#include <ipcontext.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

static Collection_t Contracts           = COLLECTION_INIT(KeyId);
static Collection_t Devices             = COLLECTION_INIT(KeyId);
static UUId_t       DeviceIdGenerator   = 0;
static UUId_t       ContractIdGenerator = 0;

OsStatus_t OnUnload(void)
{
    return OsSuccess;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_DEVICE_PATH;
}

OsStatus_t
OnLoad(void)
{
    thrd_t thr;
    
    // Register supported interfaces
    gracht_server_register_protocol(&svc_device_protocol);
    
    // Start the enumeration process in a new thread so we can quickly return
    // and be ready for requests.
    if (thrd_create(&thr, BusEnumerate, NULL) != thrd_success) {
        return OsError;
    }
    return OsSuccess;
}

void svc_device_register_callback(struct gracht_recv_message* message, struct svc_device_register_args* args)
{
    UUId_t     Result = UUID_INVALID;
    OsStatus_t Status = DmRegisterDevice(args->parent, args->device, NULL, args->flags, &Result);
    svc_device_register_response(message, Status);
}

void svc_device_unregister_callback(struct gracht_recv_message* message, struct svc_device_unregister_args* args)
{
    svc_device_unregister_response(message, OsNotSupported);
}

void svc_device_ioctl_callback(struct gracht_recv_message* message, struct svc_device_ioctl_args* args)
{
    MCoreDevice_t* Device;
    OsStatus_t     Result;
    DataKey_t      Key = { .Value.Id = args->device_id };
    
    Device = CollectionGetDataByKey(&Devices, Key, 0);
    Result = DmIoctlDevice(Device, args->command, args->flags);
    
    svc_device_ioctl_response(message, Result);
}

void svc_device_ioctl_ex_callback(struct gracht_recv_message* message, struct svc_device_ioctl_ex_args* args)
{
    MCoreDevice_t* Device;
    OsStatus_t     Result;
    DataKey_t      Key = { .Value.Id = args->device_id };
    
    Device = CollectionGetDataByKey(&Devices, Key, 0);
    Result = DmIoctlDeviceEx(Device, args->direction,
        args->command, &args->value, args->width);
    
    svc_device_ioctl_ex_response(message, Result, args->value);
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
    
    // Now, we want to try to find a driver for the new device, spawn a new thread
    // for dealing with this to avoid any waiting for the ipc to open up
#ifndef __OSCONFIG_NODRIVERS
    if (Flags & __DEVICEMANAGER_REGISTER_LOADDRIVER) {
        thrd_t thr;
        if (thrd_create(&thr, DmLoadDeviceDriver, CopyDevice) != thrd_success) {
            return OsError;
        }
        thrd_detach(thr);
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
