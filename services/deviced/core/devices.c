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
#include "../bus/pci/bus.h"

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
    unsigned int flags;
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


oserr_t
DmDevicesRegister(
    _In_ uuid_t driverHandle,
    _In_ uuid_t deviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverHandle);
    struct DMDevice*         device;
    struct sys_device        protoDevice;
    oserr_t                  status;
    int                      sent;

    // Copy the description while publication owns it. The wire copy is detached
    // from the registry before any driver message can yield to another task.
    usched_mtx_lock(&g_devicesLock);

    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL || !(device->flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }

    if (device->driver_id != UUID_INVALID) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_EEXISTS;
    }

    status = to_sys_device(device->device, &protoDevice);
    if (status != OS_EOK) {
        device->has_driver = false;
        usched_mtx_unlock(&g_devicesLock);
        return status;
    }

    device->driver_id = driverHandle;
    usched_mtx_unlock(&g_devicesLock);

    sent = ctt_driver_register_device(GetGrachtClient(), &msg.base, &protoDevice);
    if (!sent) {
        ctt_driver_get_device_protocols(GetGrachtClient(), &msg.base, deviceId);
    }

    sys_device_destroy(&protoDevice);
    if (sent) {
        usched_mtx_lock(&g_devicesLock);
        device = __GetDeviceUnsafe(deviceId);
        if (device != NULL) {
            device->driver_id = UUID_INVALID;
            device->has_driver = false;
        }
        usched_mtx_unlock(&g_devicesLock);
        return OS_EUNKNOWN;
    }
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
    BusDevice_t bus;
    uuid_t driverId;
    int isBus;

    usched_mtx_lock(&g_devicesLock);
    
    device = __GetDeviceUnsafe(deviceID);
    if (device == NULL) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }
    
    driverId = device->driver_id;
    isBus = device->device->Length == sizeof(BusDevice_t);
    if (isBus) {
        bus = *(BusDevice_t*)device->device;
    }
    
    usched_mtx_unlock(&g_devicesLock);
    
    if (request == OSIOCTLREQUEST_BUS_CONTROL) {
        if (length < sizeof(struct OSIOCtlBusControl)) {
            return OS_EINVALPARAMS;
        }
        return isBus ? DMBusControl(&bus, buffer) : OS_ENOTSUPPORTED;
    }
    
    if (request == OSIOCTLREQUEST_IO_REQUIREMENTS && driverId != UUID_INVALID) {
        return OSDeviceIOCtl2(deviceID, driverId, request, buffer, length);
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
    struct DMDevice* device;
    BusDevice_t      bus;

    usched_mtx_lock(&g_devicesLock);
    
    device = __GetDeviceUnsafe(deviceID);
    if (device == NULL || device->device->Length != sizeof(BusDevice_t)) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOTSUPPORTED;
    }
    
    bus = *(BusDevice_t*)device->device;
    usched_mtx_unlock(&g_devicesLock);
    
    *valueOut = value;
    return DmIoctlDeviceEx(
        &bus,
        direction,
        command,
        valueOut,
        width
    );
}

oserr_t
DmDeviceQuiesceInterrupts(
    _In_ const DeviceInterruptQuiesceRequest_t* request)
{
    struct DMDevice* device;
    BusDevice_t      bus;

    if (request == NULL || request->DeviceId == UUID_INVALID) {
        return OS_EINVALPARAMS;
    }

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(request->DeviceId);
    if (device == NULL || device->device->Length != sizeof(BusDevice_t)) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }
    bus = *(BusDevice_t*)device->device;
    usched_mtx_unlock(&g_devicesLock);

    if (!bus.IsPci || bus.Segment != request->Segment || bus.Bus != request->Bus ||
        bus.Slot != request->Slot || bus.Function != request->Function) {
        return OS_EPERMISSIONS;
    }
    return DmPciQuiesceDevice(&bus);
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

static void
__TryLocateDriver(
    _In_ uuid_t deviceId)
{
    struct DMDevice* device;
    struct DriverIdentification identification = { 0 };
    PlatformDevice_t* platform;
    char compatibles[PLATFORM_DEVICE_MAX_COMPATIBLES];
    oserr_t status;

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL || device->has_driver ||
        !(device->flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
        usched_mtx_unlock(&g_devicesLock);
        return;
    }
    
    identification.VendorId = device->device->VendorId;
    identification.ProductId = device->device->ProductId;
    identification.Class = device->device->Class;
    identification.Subclass = device->device->Subclass;
    if (device->device->Length == sizeof(PlatformDevice_t)) {
        platform = (PlatformDevice_t*)device->device;
        memcpy(compatibles, platform->Compatibles, platform->CompatibleLength);
        identification.IsPlatform = 1;
        identification.Compatibles = compatibles;
        identification.CompatibleLength = platform->CompatibleLength;
    }
    
    // Reserve this attempt, then release the registry lock. A ready driver can
    // call back into DmDevicesRegister, which acquires that same lock.
    device->has_driver = true;
    usched_mtx_unlock(&g_devicesLock);
    
    status = DmDiscoverFindDriver(deviceId, &identification);
    if (status != OS_EOK) {
        usched_mtx_lock(&g_devicesLock);
        device = __GetDeviceUnsafe(deviceId);
        if (device != NULL && device->driver_id == UUID_INVALID) {
            device->has_driver = false;
        }
        usched_mtx_unlock(&g_devicesLock);
    }
}

int
DmDeviceIsBindable(
    _In_ uuid_t deviceId)
{
    struct DMDevice* device;
    int bindable;

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(deviceId);
    bindable = device != NULL && (device->flags & DEVICE_REGISTER_FLAG_LOADDRIVER);
    usched_mtx_unlock(&g_devicesLock);
    return bindable;
}

oserr_t
DmDeviceEnableDriverBinding(
    _In_ uuid_t deviceId)
{
    struct DMDevice* device;

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }
    device->flags |= DEVICE_REGISTER_FLAG_LOADDRIVER;
    usched_mtx_unlock(&g_devicesLock);
#ifndef __OSCONFIG_NODRIVERS
    __TryLocateDriver(deviceId);
#endif
    return OS_EOK;
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

    if (device->Length == sizeof(PlatformDevice_t) &&
        !PlatformDeviceValidate((PlatformDevice_t*)device)) {
        return OS_EINVALPARAMS;
    }
    deviceNode = (struct DMDevice*)malloc(sizeof(struct DMDevice));
    if (!deviceNode) {
        return OS_EOOM;
    }

    // initialize object
    ELEMENT_INIT(&deviceNode->header, (uintptr_t)device->Id, deviceNode);
    deviceNode->driver_id  = UUID_INVALID;
    deviceNode->device     = device;
    deviceNode->has_driver = false;
    deviceNode->flags = flags;
    list_construct(&deviceNode->protocols);

    usched_mtx_lock(&g_devicesLock);
    device->Id = g_nextDeviceId++;
    deviceNode->header.key = (void*)(uintptr_t)device->Id;
    list_append(&g_devices, &deviceNode->header);
    usched_mtx_unlock(&g_devicesLock);
    *idOut = device->Id;

    TRACE("%u, Registered device %s, struct length %u",
          device->Id, device->Identification.Description, device->Length);

    // Match only after the owned description is visible in the registry.
#ifndef __OSCONFIG_NODRIVERS
    if (flags & DEVICE_REGISTER_FLAG_LOADDRIVER) {
        __TryLocateDriver(*idOut);
    }
#endif
    return OS_EOK;
}

void
DmDeviceRefreshDrivers(void)
{
#ifndef __OSCONFIG_NODRIVERS
    uuid_t* ids;
    size_t count = 0;
    size_t index;
    struct DMDevice* device;

    usched_mtx_lock(&g_devicesLock);
    ids = malloc(g_devices.count * sizeof(*ids));
    if (ids == NULL) {
        usched_mtx_unlock(&g_devicesLock);
        return;
    }
    foreach (i, &g_devices) {
        device = i->value;
        if (!device->has_driver && (device->flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
            ids[count++] = device->device->Id;
        }
    }
    usched_mtx_unlock(&g_devicesLock);
    for (index = 0; index < count; index++) {
        __TryLocateDriver(ids[index]);
    }
    free(ids);
#endif
}

oserr_t
DmDeviceDestroy(
    _In_ uuid_t deviceId)
{
    struct DMDevice* device;
    struct DmDeviceProtocol* protocol;
    DeviceIdentification_t* identification;

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }
    foreach (i, &g_devices) {
        if (((struct DMDevice*)i->value)->device->ParentId == deviceId) {
            usched_mtx_unlock(&g_devicesLock);
            return OS_EBUSY;
        }
    }
    list_remove(&g_devices, &device->header);
    usched_mtx_unlock(&g_devicesLock);
    // Do not hold the registry lock while cancelling discovery work. A driver
    // completing startup must see either the live device or ENOENT.
    DmDiscoverForgetDevice(deviceId);
    while (device->protocols.head != NULL) {
        protocol = device->protocols.head->value;
        list_remove(&device->protocols, &protocol->header);
        free(protocol->name);
        free(protocol);
    }
    identification = &device->device->Identification;
    free(identification->Description);
    free(identification->Manufacturer);
    free(identification->Product);
    free(identification->Revision);
    free(identification->Serial);
    free(device->device);
    free(device);
    return OS_EOK;
}
