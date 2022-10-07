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

#include <assert.h>
#include <devices.h>
#include <discover.h>
#include <requests.h>
#include <ddk/busdevice.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>

#include <sys_device_service_server.h>
#include <ctt_driver_service_client.h>

extern gracht_server_t* __crt_get_service_server(void);

struct DmDeviceProtocol {
    element_t header;
    char*     name;
};

struct DmDevice {
    element_t header;
    uuid_t    driver_id;
    Device_t* device;
    list_t    protocols;   // list<struct DmDeviceProtocol>
};

static struct usched_mtx g_devicesLock;
static list_t            g_devices      = LIST_INIT;
static uuid_t            g_nextDeviceId = 1;

void DmDevicesInitialize(void)
{
    usched_mtx_init(&g_devicesLock);
}

static struct DmDevice*
__GetDevice(
        _In_ uuid_t deviceId)
{
    struct DmDevice* result = NULL;
    usched_mtx_lock(&g_devicesLock);
    foreach (i, &g_devices) {
        struct DmDevice* device = i->value;
        if (device->device->Id == deviceId) {
            result = device;
            break;
        }
    }
    usched_mtx_unlock(&g_devicesLock);
    return result;
}

oserr_t
DmDevicesRegister(
        _In_ uuid_t driverHandle,
        _In_ uuid_t deviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverHandle);
    struct DmDevice*         device = __GetDevice(deviceId);
    struct sys_device        protoDevice;
    TRACE("DmDevicesRegister(driverHandle=%u, deviceId=%u)",
          driverHandle, deviceId);

    if (!device) {
        return OsNotExists;
    }

    // store the driver loaded
    device->driver_id = driverHandle;

    to_sys_device(device->device, &protoDevice);
    ctt_driver_register_device(GetGrachtClient(), &msg.base, &protoDevice);
    ctt_driver_get_device_protocols(GetGrachtClient(), &msg.base, device->device->Id);
    sys_device_destroy(&protoDevice);
    return OsOK;
}

void DmHandleDeviceCreate(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    uuid_t    result = UUID_INVALID;
    oserr_t   status;
    Device_t* device;

    device = from_sys_device(&request->parameters.create.device);
    if (device == NULL) {
        status = OsInvalidParameters;
        goto respond;
    }

    status = DmDeviceCreate(
            device,
            request->parameters.create.flags,
            &result
    );

respond:
    sys_device_register_response(request->message, status, result);

    sys_device_destroy(&request->parameters.create.device);
    RequestDestroy(request);
}

void DmHandleDeviceDestroy(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    sys_device_unregister_response(request->message, OsNotSupported);
    RequestDestroy(request);
}

void DmHandleGetDevicesByProtocol(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    TRACE("DmHandleGetDevicesByProtocol(protocol=%u)",
          request->parameters.get_devices_by_protocol.protocol);

    usched_mtx_lock(&g_devicesLock);
    foreach(node, &g_devices) {
        struct DmDevice* device = node->value;
        foreach(protoNode, &device->protocols) {
            struct DmDeviceProtocol* protocol = protoNode->value;
            uint8_t                  id = (uint8_t)(uintptr_t)protocol->header.key;
            if (id == request->parameters.get_devices_by_protocol.protocol) {
                sys_device_event_protocol_device_single(
                        __crt_get_service_server(),
                        request->message[0].client,
                        device->device->Id,
                        device->driver_id,
                        request->parameters.get_devices_by_protocol.protocol
                );
            }
        }
    }
    usched_mtx_unlock(&g_devicesLock);

    RequestDestroy(request);
}

void DmHandleIoctl(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    struct DmDevice* device = __GetDevice(request->parameters.ioctl.device_id);
    oserr_t       result = OsInvalidParameters;

    if (device && device->device->Length == sizeof(BusDevice_t)) {
        result = DmIoctlDevice(
                (BusDevice_t*)device->device,
                request->parameters.ioctl.command,
                request->parameters.ioctl.flags
        );
    }

    sys_device_ioctl_response(request->message, result);
    RequestDestroy(request);
}

void DmHandleIoctl2(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    struct DmDevice* device  = __GetDevice(request->parameters.ioctl2.device_id);
    oserr_t       result  = OsInvalidParameters;
    size_t           storage = request->parameters.ioctl2.value;

    if (device && device->device->Length == sizeof(BusDevice_t)) {
        result = DmIoctlDeviceEx(
                (BusDevice_t*)device->device,
                request->parameters.ioctl2.direction,
                request->parameters.ioctl2.command,
                &storage,
                request->parameters.ioctl2.width
        );
    }

    sys_device_ioctlex_response(request->message, result, (size_t)storage);
    RequestDestroy(request);
}

static void
__AddProtocolToDevice(
        _In_ Request_t*       request,
        _In_ struct DmDevice* device)
{
    struct DmDeviceProtocol* protocol = malloc(sizeof(struct DmDeviceProtocol));
    if (!protocol) {
        return;
    }

    ELEMENT_INIT(&protocol->header, (uintptr_t)request->parameters.register_protocol.protocol_id, protocol);
    protocol->name = strdup(request->parameters.register_protocol.protocol_name);
    list_append(&device->protocols, &protocol->header);
}

void DmHandleRegisterProtocol(
        _In_ Request_t* request,
        _In_ void*      cancellationToken)
{
    struct DmDevice* device = __GetDevice(request->parameters.ioctl2.device_id);
    if (device) {
        __AddProtocolToDevice(request, device);
    }

    free((void*)request->parameters.register_protocol.protocol_name);
    RequestDestroy(request);
}

oserr_t
DmDeviceCreate(
        _In_  Device_t*    device,
        _In_  unsigned int flags,
        _Out_ uuid_t*      idOut)
{
    struct DmDevice* deviceNode;

    assert(device != NULL);
    assert(idOut != NULL);
    assert(device->Length >= sizeof(Device_t));

    deviceNode = (struct DmDevice*)malloc(sizeof(struct DmDevice));
    if (!deviceNode) {
        return OsOutOfMemory;
    }

    // assign a unique device id for this instance
    device->Id = g_nextDeviceId++;

    // initialize object
    ELEMENT_INIT(&deviceNode->header, (uintptr_t)device->Id, deviceNode);
    deviceNode->driver_id = UUID_INVALID;
    deviceNode->device    = device;
    list_construct(&deviceNode->protocols);

    list_append(&g_devices, &deviceNode->header);
    *idOut = device->Id;

    TRACE("%u, Registered device %s, struct length %u",
          device->Id, device->Identification.Description, device->Length);

    // Now, we want to try to find a driver for the new device, spawn a new thread
    // for dealing with this to avoid any waiting for the ipc to open up
#ifndef __OSCONFIG_NODRIVERS
    if (flags & DEVICE_REGISTER_FLAG_LOADDRIVER) {
        struct DriverIdentification driverIdentification = {
                .VendorId = device->VendorId,
                .ProductId = device->ProductId,

                .Class = device->Class,
                .Subclass = device->Subclass
        };

        DmDiscoverFindDriver(device->Id, &driverIdentification);
    }
#endif
    return OsOK;
}
