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
#include <ddk/busdevice.h>
#include <ddk/utils.h>
#include <ds/list.h>
#include <gracht/link/vali.h>
#include <internal/_ipc.h>
#include <os/mollenos.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <svc_device_protocol_server.h>
static void svc_device_notify_callback(struct gracht_recv_message* message, struct svc_device_notify_args*);
static void svc_device_register_callback(struct gracht_recv_message* message, struct svc_device_register_args*);
static void svc_device_unregister_callback(struct gracht_recv_message* message, struct svc_device_unregister_args*);
static void svc_device_ioctl_callback(struct gracht_recv_message* message, struct svc_device_ioctl_args*);
static void svc_device_ioctl_ex_callback(struct gracht_recv_message* message, struct svc_device_ioctl_ex_args*);
static void svc_device_get_devices_by_protocol_callback(struct gracht_recv_message* message, struct svc_device_get_devices_by_protocol_args*);

static gracht_protocol_function_t svc_device_functions[6] = {
    { PROTOCOL_SVC_DEVICE_NOTIFY_ID , svc_device_notify_callback },
    { PROTOCOL_SVC_DEVICE_REGISTER_ID , svc_device_register_callback },
    { PROTOCOL_SVC_DEVICE_UNREGISTER_ID , svc_device_unregister_callback },
    { PROTOCOL_SVC_DEVICE_IOCTL_ID , svc_device_ioctl_callback },
    { PROTOCOL_SVC_DEVICE_IOCTL_EX_ID , svc_device_ioctl_ex_callback },
    { PROTOCOL_SVC_DEVICE_GET_DEVICES_BY_PROTOCOL_ID , svc_device_get_devices_by_protocol_callback },
};
DEFINE_SVC_DEVICE_SERVER_PROTOCOL(svc_device_functions, 6);

#include <ctt_driver_protocol_client.h>

static void ctt_driver_event_device_protocol_callback(struct ctt_driver_device_protocol_event*);

static gracht_protocol_function_t ctt_driver_callbacks[1] = {
    { PROTOCOL_CTT_DRIVER_EVENT_DEVICE_PROTOCOL_ID , ctt_driver_event_device_protocol_callback },
};
DEFINE_CTT_DRIVER_CLIENT_PROTOCOL(ctt_driver_callbacks, 1);

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
    gracht_server_register_protocol(&svc_device_server_protocol);

    // Register the client control protocol
    gracht_client_register_protocol(GetGrachtClient(), &ctt_driver_client_protocol);

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
        
        // Check against vendor/device ids if nonne-zero
        if (driverNode->vendor_id != 0 && driverNode->device_id != 0) {
            if (device->VendorId == driverNode->vendor_id &&
                device->DeviceId == driverNode->device_id) {
                return driverNode;     
            }
        }
        
        // Otherwise match against class/subclass to identify generic match
        if (device->Class    == driverNode->class &&
            device->Subclass == driverNode->sub_class) {
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
                UUId_t                   handle = (UUId_t)(uintptr_t)driverNode->header.key;
                struct vali_link_message msg    = VALI_MSG_INIT_HANDLE(handle);
                TRACE("[devicemanager] [notify] found device for driver: %s",
                    &deviceNode->device->Name[0]);
                ctt_driver_register_device(GetGrachtClient(), &msg.base, deviceNode->device,
                    deviceNode->device->Length);
                
                deviceNode->driver_id = handle;
            }
        }
    }
}

void svc_device_notify_callback(struct gracht_recv_message* message, struct svc_device_notify_args* args)
{
    struct driver_node* driverNode;
    
    TRACE("[devicemanager] [notify] driver registered for [%u:%u %u:%u]",
        args->vendor_id, args->device_id, args->class, args->subclass);

    driverNode = (struct driver_node*)malloc(sizeof(struct driver_node));
    if (!driverNode) {
        ERROR("[devicemanager] [notify] failed to allocate memory for driver node");
        return;
    }
    
    ELEMENT_INIT(&driverNode->header, (uintptr_t)args->driver, driverNode);
    driverNode->vendor_id = args->vendor_id;
    driverNode->device_id = args->device_id;
    driverNode->class = args->class;
    driverNode->sub_class = args->subclass;
    list_append(&Drivers, &driverNode->header);
    
    // Perform an update run of device/drivers
    update_device_drivers();
}

void svc_device_register_callback(struct gracht_recv_message* message, struct svc_device_register_args* args)
{
    UUId_t     result = UUID_INVALID;
    OsStatus_t status = DmRegisterDevice(args->device, NULL, args->flags, &result);
    svc_device_register_response(message, status, result);
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

void svc_device_get_devices_by_protocol_callback(
        struct gracht_recv_message* message,
        struct svc_device_get_devices_by_protocol_args* args)
{
    foreach(node, &Devices) {
        struct device_node* deviceNode = node->value;
        // if (device_supports_node(deviceNode, args->protocol_id)) {
        //  protocol_device(device->device_id, args->protocol_id);
        //}
    }
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
    _In_  Device_t*    device,
    _In_  const char*  name,
    _In_  unsigned int flags,
    _Out_ UUId_t*      idOut)
{
    struct device_node* deviceNode;
    UUId_t              deviceId;

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

static void ctt_driver_event_device_protocol_callback(struct ctt_driver_device_protocol_event* args)
{

}
