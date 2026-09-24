/**
 * Copyright 2021, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

#define __TRACE
//#define __OSCONFIG_NODRIVERS

#include <assert.h>
#include <devices.h>
#include <discover.h>
#include <ddk/busdevice.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/usched/mutex.h>
#include <os/device.h>

#include <sys_device_service_server.h>
#include <ctt_driver_service_client.h>

extern gracht_server_t* __crt_get_service_server(void);

struct DmDeviceProtocol {
    element_t header;
    char*     name;
};

struct DMDevice {
    element_t header;
    uuid_t    driver_id;
    bool      has_driver;
    Device_t* device;
    list_t    protocols;   // list<struct DmDeviceProtocol>
};

static struct usched_mtx g_devicesLock;
static list_t            g_devices      = LIST_INIT;
static uuid_t            g_nextDeviceId = 1;

void DmDevicesInitialize(void)
{
    usched_mtx_init(&g_devicesLock, USCHED_MUTEX_PLAIN);
}

static struct DMDevice*
__GetDeviceUnsafe(
    _In_ uuid_t deviceId)
{
    struct DMDevice* result = NULL;
    foreach (i, &g_devices) {
        struct DMDevice* device = i->value;
        if (device->device->Id == deviceId) {
            result = device;
            break;
        }
    }
    return result;
}

static struct DMDevice*
__GetDevice(uuid_t deviceId)
{
    struct DMDevice* device;

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(deviceId);
    usched_mtx_unlock(&g_devicesLock);
    return device;
}

oserr_t
DmDevicesRegister(
    _In_ uuid_t driverHandle,
    _In_ uuid_t deviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverHandle);
    struct DMDevice*         device = __GetDevice(deviceId);
    struct sys_device        protoDevice;
    TRACE("DmDevicesRegister(driverHandle=%u, deviceId=%u)",
          driverHandle, deviceId);

    if (!device) {
        return OS_ENOENT;
    }

    // store the driver loaded
    usched_mtx_lock(&g_devicesLock);
    device->driver_id = driverHandle;
    usched_mtx_unlock(&g_devicesLock);

    to_sys_device(device->device, &protoDevice);
    ctt_driver_register_device(GetGrachtClient(), &msg.base, &protoDevice);
    ctt_driver_get_device_protocols(GetGrachtClient(), &msg.base, device->device->Id);
    sys_device_destroy(&protoDevice);
    return OS_EOK;
}

void DmHandleGetDevicesByProtocol(
    _In_ struct gracht_message* message,
    _In_ uint8_t                protocolID)
{
    TRACE("DmHandleGetDevicesByProtocol(protocol=%u)", protocolID);

    usched_mtx_lock(&g_devicesLock);
    foreach(node, &g_devices) {
        struct DMDevice* device = node->value;
        foreach(protoNode, &device->protocols) {
            struct DmDeviceProtocol* protocol = protoNode->value;
            uint8_t                  id = (uint8_t)(uintptr_t)protocol->header.key;
            if (id == protocolID) {
                sys_device_event_protocol_device_single(
                    __crt_get_service_server(),
                    message->client,
                    device->device->Id,
                    device->driver_id,
                    protocolID
                );
            }
        }
    }
    usched_mtx_unlock(&g_devicesLock);
}

oserr_t
DmHandleIoctl(
    _In_ uuid_t              deviceID,
    _In_ enum OSIOCtlRequest request,
    _In_ void*               buffer,
    _In_ size_t              length)
{
    struct DMDevice* device;

    device = __GetDevice(deviceID);
    if (device == NULL) {
        return OS_ENOENT;
    }

    switch (request) {
        // Device manager handled requests
        case OSIOCTLREQUEST_BUS_CONTROL: {
            struct OSIOCtlBusControl* data = buffer;
            if (length < sizeof(struct OSIOCtlBusControl)) {
                return OS_EINVALPARAMS;
            }
            return DMBusControl((BusDevice_t*)device->device, data);
        }

        // Device driver handled requests
        case OSIOCTLREQUEST_IO_REQUIREMENTS: {
            return OSDeviceIOCtl2(
                deviceID,
                device->driver_id,
                request,
                buffer,
                length
            );
        }

        default:
            break;
    }
    return OS_ENOTSUPPORTED;
}

oserr_t
DmHandleIoctl2(
    _In_  uuid_t       deviceID,
    _In_  int          direction,
    _In_  unsigned int command,
    _In_  size_t       value,
    _In_  unsigned int width,
    _Out_ size_t*      valueOut)
{
    struct DMDevice* device = __GetDevice(deviceID);
    oserr_t          result  = OS_EINVALPARAMS;
    size_t           storage = value;

    if (device && device->device->Length == sizeof(BusDevice_t)) {
        result = DmIoctlDeviceEx(
            (BusDevice_t*)device->device,
            direction,
            command,
            &storage,
            width
        );
        *valueOut = storage;
    }
    return result;
}

static oserr_t
__AddProtocolToDevice(
    _In_ const char*      protocolName,
    _In_ uint8_t          protocolID,
    _In_ struct DMDevice* device)
{
    struct DmDeviceProtocol* protocol;

    // Check if the protocol is already added to the device.
    foreach (node, &device->protocols) {
        protocol = node->value;
        if ((uint8_t)(uintptr_t)protocol->header.key == protocolID) {
            return OS_EEXISTS;
        }
    }
    
    protocol = malloc(sizeof(*protocol));
    if (protocol == NULL) {
        return OS_EOOM;
    }
    
    protocol->name = strdup(protocolName);
    if (protocol->name == NULL) {
        free(protocol);
        return OS_EOOM;
    }
    
    ELEMENT_INIT(&protocol->header, (uintptr_t)protocolID, protocol);
    list_append(&device->protocols, &protocol->header);
    return OS_EOK;
}

void DmHandleRegisterProtocol(
    _In_ uuid_t      deviceID,
    _In_ const char* protocolName,
    _In_ uint8_t     protocolID)
{
    struct DMDevice* device;
    oserr_t          oserr;

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(deviceID);
    if (device == NULL || device->driver_id == UUID_INVALID) {
        usched_mtx_unlock(&g_devicesLock);
        return;
    }

    // Attempt to add the protocol to the device.
    oserr = __AddProtocolToDevice(protocolName, protocolID, device);
    if (oserr == OS_EOK) {
        // Subscribers may have queried before this driver finished attaching.
        sys_device_event_protocol_device_all(
            __crt_get_service_server(),
            deviceID,
            device->driver_id,
            protocolID
        );
    }
    usched_mtx_unlock(&g_devicesLock);
}

static void __TryLocateDriver(
    _In_ struct DMDevice* device)
{
    struct DriverIdentification driverIdentification = {
        .VendorId = device->device->VendorId,
        .ProductId = device->device->ProductId,

        .Class = device->device->Class,
        .Subclass = device->device->Subclass
    };

    oserr_t oserr = DmDiscoverFindDriver(device->device->Id, &driverIdentification);
    if (oserr == OS_EOK) {
        // a driver was found for the device
        device->has_driver = true;
    }
}

oserr_t
DmDeviceCreate(
    _In_  Device_t*    device,
    _In_  unsigned int flags,
    _Out_ uuid_t*      idOut)
{
    struct DMDevice* deviceNode;

    assert(device != NULL);
    assert(idOut != NULL);
    assert(device->Length >= sizeof(Device_t));

    deviceNode = (struct DMDevice*)malloc(sizeof(struct DMDevice));
    if (!deviceNode) {
        return OS_EOOM;
    }

    // assign a unique device id for this instance
    device->Id = g_nextDeviceId++;

    // initialize object
    ELEMENT_INIT(&deviceNode->header, (uintptr_t)device->Id, deviceNode);
    deviceNode->driver_id  = UUID_INVALID;
    deviceNode->device     = device;
    deviceNode->has_driver = false;
    list_construct(&deviceNode->protocols);

    usched_mtx_lock(&g_devicesLock);
    list_append(&g_devices, &deviceNode->header);
    usched_mtx_unlock(&g_devicesLock);
    *idOut = device->Id;

    TRACE("%u, Registered device %s, struct length %u",
          device->Id, device->Identification.Description, device->Length);

    // Now, we want to try to find a driver for the new device, spawn a new thread
    // for dealing with this to avoid any waiting for the ipc to open up
#ifndef __OSCONFIG_NODRIVERS
    if (flags & DEVICE_REGISTER_FLAG_LOADDRIVER) {
        __TryLocateDriver(deviceNode);
    }
#endif
    return OS_EOK;
}

void DmDeviceRefreshDrivers(void)
{
    TRACE("DmDeviceRefreshDrivers()");
#ifndef __OSCONFIG_NODRIVERS
    usched_mtx_lock(&g_devicesLock);
    foreach (i, &g_devices) {
        struct DMDevice* device = i->value;
        TRACE("DmDeviceRefreshDrivers device=%s, hasDriver=%i",
              device->device->Identification.Description, device->has_driver);
        if (device->has_driver) {
            continue;
        }
        __TryLocateDriver(device);
    }
    usched_mtx_unlock(&g_devicesLock);
#endif
}
