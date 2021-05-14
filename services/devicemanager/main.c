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

#include <sys_device_service_server.h>
#include <ctt_driver_service_client.h>

extern gracht_server_t* __crt_get_service_server(void);

struct device_protocol {
    element_t header;
    char*     name;
};

struct device_node {
    element_t header;
    UUId_t    driver_id;
    Device_t* device;
    list_t    protocols;
};

struct driver_node {
    element_t    header;
    unsigned int vendor_id;
    unsigned int product_id;
    
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
    gracht_server_register_protocol(__crt_get_service_server(), &sys_device_server_protocol);

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
        if (driverNode->vendor_id != 0 && driverNode->product_id != 0) {
            if (device->VendorId == driverNode->vendor_id &&
                device->ProductId == driverNode->product_id) {
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
                ctt_driver_register_device(GetGrachtClient(), &msg.base, (uint8_t*)deviceNode->device,
                    deviceNode->device->Length);
                ctt_driver_get_device_protocols(GetGrachtClient(), &msg.base, deviceNode->device->Id);
                
                deviceNode->driver_id = handle;
            }
        }
    }
}

void sys_device_notify_invocation(struct gracht_message* message,
        const UUId_t driverId, const unsigned int vendorId,
        const unsigned int productId, const unsigned int class,
        const unsigned int subclass)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverId);
    struct driver_node*      driverNode;
    
    TRACE("[devicemanager] [notify] driver registered for [%u:%u %u:%u]",
        vendorId, productId, class, subclass);

    driverNode = (struct driver_node*)malloc(sizeof(struct driver_node));
    if (!driverNode) {
        ERROR("[devicemanager] [notify] failed to allocate memory for driver node");
        return;
    }
    
    ELEMENT_INIT(&driverNode->header, (uintptr_t)driverId, driverNode);
    driverNode->vendor_id = vendorId;
    driverNode->product_id = productId;
    driverNode->class = class;
    driverNode->sub_class = subclass;
    list_append(&Drivers, &driverNode->header);

    // Subscribe to events from this source
    ctt_driver_subscribe(GetGrachtClient(), &msg.base);
    
    // Perform an update run of device/drivers
    update_device_drivers();
}

void sys_device_register_invocation(struct gracht_message* message, const uint8_t* buffer, const uint32_t buffer_count, const unsigned int flags)
{
    UUId_t     result = UUID_INVALID;
    OsStatus_t status = DmRegisterDevice((Device_t*)buffer, NULL, flags, &result);
    sys_device_register_response(message, status, result);
}

void sys_device_unregister_invocation(struct gracht_message* message, const UUId_t deviceId)
{
    sys_device_unregister_response(message, OsNotSupported);
}

void sys_device_ioctl_invocation(struct gracht_message* message, const UUId_t deviceId, const unsigned int command, const unsigned int flags)
{
    struct device_node* deviceNode = (struct device_node*)list_find(&Devices, (void*)(uintptr_t)deviceId);
    OsStatus_t          result     = OsInvalidParameters;

    if (deviceNode && deviceNode->device->Length == sizeof(BusDevice_t)) {
        result = DmIoctlDevice((BusDevice_t*)deviceNode->device, command, flags);
    }
    
    sys_device_ioctl_response(message, result);
}

void sys_device_ioctlex_invocation(struct gracht_message* message, const UUId_t deviceId,
        const int direction, const unsigned int command,
        const size_t value, const unsigned int width)
{
    struct device_node* deviceNode = (struct device_node*)list_find(&Devices, (void*)(uintptr_t)deviceId);
    OsStatus_t          result     = OsInvalidParameters;
    uint64_t            storage    = (uint64_t)value;
    
    if (deviceNode && deviceNode->device->Length == sizeof(BusDevice_t)) {
        result = DmIoctlDeviceEx((BusDevice_t*)deviceNode->device, direction, command, &storage, width);
    }
    
    sys_device_ioctlex_response(message, result, (size_t)storage);
}

void sys_device_get_devices_by_protocol_invocation(struct gracht_message* message, const uint8_t protocolId)
{
    TRACE("[svc_device_get_devices_by_protocol_callback] %u", protocolId);
    foreach(node, &Devices) {
        struct device_node* deviceNode = node->value;
        foreach(protoNode, &deviceNode->protocols) {
            struct device_protocol* protocol = protoNode->value;
            if ((uintptr_t)protocol->header.key == (uintptr_t)protocolId) {
                sys_device_event_protocol_device_single(__crt_get_service_server(),
                                                        message->client, deviceNode->device->Id,
                                                        deviceNode->driver_id, protocolId);
            }
        }
    }
}

static int __LoadDriverWorker(void* context)
{
    Device_t*  device = context;
    OsStatus_t osStatus;

    osStatus = InstallDriver(device, device->Length, NULL, 0);
    if (osStatus != OsSuccess) {
        return OsStatusToErrno(osStatus);
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
    Device_t*           deviceCopy;

    assert(device != NULL);
    assert(idOut != NULL);
    assert(device->Length >= sizeof(Device_t));

    deviceNode = (struct device_node*)malloc(sizeof(struct device_node));
    if (!deviceNode) {
        return OsOutOfMemory;
    }

    deviceCopy = (Device_t*)malloc(device->Length);
    if (!deviceCopy) {
        free(deviceNode);
        return OsOutOfMemory;
    }

    // Create the device cloned object and adjust name/id
    memcpy(deviceCopy, device, device->Length);
    if (name != NULL) {
        strncpy(&deviceCopy->Name[0], name, sizeof(deviceCopy->Name));
    }
    deviceCopy->Id = DeviceIdGenerator++;

    // initialize object
    ELEMENT_INIT(&deviceNode->header, (uintptr_t)deviceCopy->Id, deviceNode);
    deviceNode->driver_id = UUID_INVALID;
    deviceNode->device    = deviceCopy;
    list_construct(&deviceNode->protocols);

    list_append(&Devices, &deviceNode->header);
    *idOut = deviceCopy->Id;

    TRACE("%u, Registered device %s, struct length %u",
          deviceCopy->Id, &deviceCopy->Name[0], deviceCopy->Length);

    // Now, we want to try to find a driver for the new device, spawn a new thread
    // for dealing with this to avoid any waiting for the ipc to open up
#ifndef __OSCONFIG_NODRIVERS
    if (flags & DEVICE_REGISTER_FLAG_LOADDRIVER) {
        thrd_t thr;
        if (thrd_create(&thr, __LoadDriverWorker, deviceNode->device) != thrd_success) {
            return OsError;
        }
        thrd_detach(thr);
    }
#endif
    return OsSuccess;
}

static void ctt_driver_event_device_protocol_invocation(gracht_client_t* client,
                                                        const UUId_t deviceId,
                                                        const char* protocolName,
                                                        const uint8_t protocolId)
{
    struct device_node* deviceNode = (struct device_node*)list_find(&Devices, (void*)(uintptr_t)deviceId);

    TRACE("[ctt_driver_event_device_protocol_callback] %u => %s", deviceId, protocolName);
    if (deviceNode) {
        struct device_protocol* protocol = malloc(sizeof(struct device_protocol));
        if (!protocol) {
            return;
        }

        ELEMENT_INIT(&protocol->header, (uintptr_t)protocolId, protocol);
        protocol->name = strdup(&protocolName[0]);
        list_append(&deviceNode->protocols, &protocol->header);
    }
}
