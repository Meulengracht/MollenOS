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
#include <ddk/busdevice.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include <ipcontext.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

struct device_node {
    element_t header;
    UUId_t    driver_id;
    Device_t* device;
};

struct driver_node {
    element_t    header;
    unsigned int vendor_id;
    unsigned int device_id;
    
    unsigned int class;
    unsigned int sub_class;
};

static list_t Devices           = LIST_INIT;
static list_t Drivers           = LIST_INIT;
static UUId_t DeviceIdGenerator = 1;

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

static struct driver_node*
find_driver_for_device(
    _In_ Device_t* device)
{
    foreach(node, &Drivers) {
        struct driver_node* driverNode = node->value;
        if ((device->VendorId == driverNode->vendor_id &&
             device->DeviceId == driverNode->device_id) ||
            (device->Class == driverNode->class &&
             device->Subclass == driverNode->sub_class)) {
            return driverNode;
        }
    }
    return NULL;
}

static void
update_device_drivers(void)
{
    foreach(node, &Devices) {
        struct device_node* deviceNode = node->value;
        if (deviceNode->driver_id == UUID_INVALID) {
            struct driver_node* driverNode = find_driver_for_device(deviceNode->device);
            if (driverNode) {
                TRACE("[devicemanager] [notify] found device for driver: %s",
                    &deviceNode->device->Name[0]);
            }
        }
    }
}

void svc_device_notify_callback(struct gracht_recv_message* message, struct svc_device_notify_args* args)
{
    TRACE("[devicemanager] [notify] [%u:%u %u:%u]",
        args->vendor_id, args->device_id, args->class, args->subclass);
    
    foreach(node, &Devices) {
        struct device_node* deviceNode = node->value;
        if ((deviceNode->device->VendorId == args->vendor_id &&
            deviceNode->device->DeviceId == args->device_id) ||
            (deviceNode->device->Class == args->class &&
            deviceNode->device->Subclass == args->subclass)) {
            TRACE("[devicemanager] [notify] found device for driver: %s",
                &deviceNode->device->Name[0]);
        }
    }
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
    struct device_node* deviceNode = (struct device_node*)list_find(&Devices, (void*)(uintptr_t)args->device_id);
    OsStatus_t          result     = OsInvalidParameters;

    if (deviceNode && deviceNode->device->Length == sizeof(BusDevice_t)) {
        result = DmIoctlDevice((BusDevice_t*)deviceNode->device, args->command, args->flags);
    }
    
    svc_device_ioctl_response(message, result);
}

void svc_device_ioctl_ex_callback(struct gracht_recv_message* message, struct svc_device_ioctl_ex_args* args)
{
    struct device_node* deviceNode = (struct device_node*)list_find(&Devices, (void*)(uintptr_t)args->device_id);
    OsStatus_t          result     = OsInvalidParameters;
    
    if (deviceNode && deviceNode->device->Length == sizeof(BusDevice_t)) {
        result = DmIoctlDeviceEx((BusDevice_t*)deviceNode->device, args->direction,
            args->command, &args->value, args->width);
    }
    
    svc_device_ioctl_ex_response(message, result, args->value);
}

int
DmLoadDeviceDriver(void* Context)
{
    Device_t*  Device = Context;
    OsStatus_t Status = InstallDriver(Device, Device->Length, NULL, 0);
    
    if (Status != OsSuccess) {
        return OsStatusToErrno(Status);
    }
    return 0;
}

OsStatus_t
DmRegisterDevice(
    _In_  UUId_t      parent,
    _In_  Device_t*   device, 
    _In_  const char* name,
    _In_  Flags_t     flags,
    _Out_ UUId_t*     idOut)
{
    struct device_node* deviceNode;
    UUId_t              deviceId;

    _CRT_UNUSED(parent);
    assert(device != NULL);
    assert(idOut != NULL);
    assert(device->Length >= sizeof(Device_t));

    deviceNode = (struct device_node*)malloc(sizeof(struct device_node));
    if (!deviceNode) {
        return OsOutOfMemory;
    }
    
    deviceNode->device = (Device_t*)malloc(device->Length);
    if (!deviceNode->device) {
        free(deviceNode);
        return OsOutOfMemory;
    }
    
    deviceId = DeviceIdGenerator++;
    
    ELEMENT_INIT(&deviceNode->header, (uintptr_t)deviceId, deviceNode);
    deviceNode->driver_id = UUID_INVALID;
    
    memcpy(deviceNode->device, device, device->Length);
    if (name != NULL) {
        memcpy(&deviceNode->device->Name[0], name,
            strnlen(name, sizeof(deviceNode->device->Name) - 0));
    }
    
    TRACE("%u, Registered device %s, struct length %u", 
        deviceId, &deviceNode->device->Name[0], deviceNode->device->Length);
    
    list_append(&Devices, &deviceNode->header);
    deviceNode->device->Id = deviceId;
    *idOut = deviceId;
    
    // Now, we want to try to find a driver for the new device, spawn a new thread
    // for dealing with this to avoid any waiting for the ipc to open up
#ifndef __OSCONFIG_NODRIVERS
    if (flags & DEVICE_REGISTER_FLAG_LOADDRIVER) {
        thrd_t thr;
        if (thrd_create(&thr, DmLoadDeviceDriver, deviceNode->device) != thrd_success) {
            return OsError;
        }
        thrd_detach(thr);
    }
#endif
    return OsSuccess;
}
