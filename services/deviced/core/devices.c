/**
 * Copyright, Philip Meulengracht
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

//#define __TRACE
//#define __OSCONFIG_NODRIVERS

#include <assert.h>
#include <core/dma.h>
#include "device-private.h"
#include <limits.h>
#include <devices.h>
#include <discover.h>
#include <ddk/devices/bus.h>
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <os/usched/mutex.h>
#include <os/device.h>

#include <sys_device_service_server.h>
#include <ctt_driver_service_client.h>

extern gracht_server_t* __crt_get_service_server(void);

struct __DmDeviceProtocol {
    element_t Header;
    char*     Name;
};

struct usched_mtx g_dmDevicesLock;
static list_t g_devices      = LIST_INIT;
static uuid_t g_nextDeviceId = 1;

void
DmDevicesInitialize(void)
{
    TRACE("DmDevicesInitialize()");
    usched_mtx_init(&g_dmDevicesLock, USCHED_MUTEX_PLAIN);
    DmDmaSessionsInitialize();
}

// Requires g_dmDevicesLock. The returned entry stays valid while that lock
// is held or while an active request or DMA lease keeps it from being freed.
DmDevice_t*
DmDeviceFindLocked(
    _In_ uuid_t deviceId)
{
    DmDevice_t* result = NULL;
    DmDevice_t* device;

    foreach (i, &g_devices) {
        device = i->value;
        if (device->Device->Id == deviceId) {
            result = device;
            break;
        }
    }
    return result;
}


oserr_t
DmDevicesRegister(
    _In_ uint64_t principal,
    _In_ uuid_t   driverHandle,
    _In_ uuid_t   deviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverHandle);
    DmDevice_t*              device;
    struct sys_device        protoDevice;
    oserr_t                  status;
    int                      sent;
    TRACE("DmDevicesRegister(driver=%u, device=%u)", driverHandle, deviceId);

    // Copy the description under the registry lock. Sending a driver message
    // may yield, so the message must use its own copy after the lock is released.
    usched_mtx_lock(&g_dmDevicesLock);

    device = DmDeviceFindLocked(deviceId);
    if (device == NULL || device->Removing || !(device->Flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
        TRACE("DmDevicesRegister: device %u is not available for binding", deviceId);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_ENOENT;
    }

    if (device->DriverId != UUID_INVALID) {
        TRACE("DmDevicesRegister: device %u already has driver %u", deviceId, device->DriverId);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_EEXISTS;
    }

    status = to_sys_device(device->Device, &protoDevice);
    if (status != OS_EOK) {
        ERROR("DmDevicesRegister: failed to serialize device %u for driver %u: %u",
              deviceId, driverHandle, status);
        device->HasDriver = false;
        usched_mtx_unlock(&g_dmDevicesLock);
        return status;
    }

    // Publish both fields before sending: the driver may immediately use the
    // device after receiving its registration message.
    device->DriverId = driverHandle;
    device->DriverPrincipal = principal;
    usched_mtx_unlock(&g_dmDevicesLock);

    sent = ctt_driver_register_device(GetGrachtClient(), &msg.base, &protoDevice);
    if (sent != 0) {
        WARNING("DmDevicesRegister: sending device %u to driver %u failed: %i",
                deviceId, driverHandle, sent);
        sys_device_destroy(&protoDevice);
        usched_mtx_lock(&g_dmDevicesLock);
        device = DmDeviceFindLocked(deviceId);
        if (device != NULL) {
            device->DriverId = UUID_INVALID;
            device->DriverPrincipal = 0;
            device->HasDriver = false;
        }
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_EUNKNOWN;
    }

    // Query supported protocols after registration. A failed query does not
    // undo the driver's registration.
    sent = ctt_driver_get_device_protocols(GetGrachtClient(), &msg.base, deviceId);
    if (sent != 0) {
        WARNING("DmDevicesRegister: protocol query for device %u, driver %u failed: %i",
                deviceId, driverHandle, sent);
    }
    sys_device_destroy(&protoDevice);
    TRACE("DmDevicesRegister: device %u sent to driver %u", deviceId, driverHandle);
    return OS_EOK;
}

void
DmHandleGetDevicesByProtocol(
    _In_ struct gracht_message* message,
    _In_ uint8_t                protocolID)
{
    int status;
    TRACE("DmHandleGetDevicesByProtocol(protocol=%u)", protocolID);

    usched_mtx_lock(&g_dmDevicesLock);
    foreach (node, &g_devices) {
        DmDevice_t* device = node->value;

        if (device->Removing) {
            continue;
        }

        foreach (protoNode, &device->Protocols) {
            struct __DmDeviceProtocol* protocol = protoNode->value;
            uint8_t                    id = (uint8_t)(uintptr_t)protocol->Header.key;

            if (id == protocolID) {
                status = sys_device_event_protocol_device_single(
                    __crt_get_service_server(),
                    message->client,
                    device->Device->Id,
                    device->DriverId,
                    protocolID
                );
                if (status != 0) {
                    WARNING("DmHandleGetDevicesByProtocol: sending device %u for protocol %u failed: %i",
                            device->Device->Id, protocolID, status);
                }
            }
        }
    }

    usched_mtx_unlock(&g_dmDevicesLock);
}

// On success, the caller must later drop this request with
// DmDeviceRequestRelease (or swap it for a DMA lease). Incrementing
// ActiveRequests keeps the device entry alive once g_dmDevicesLock is released.
oserr_t
DmDeviceRequestAcquire(
    _In_  uuid_t       deviceId,
    _Out_ DmDevice_t** deviceOut)
{
    DmDevice_t* device;

    usched_mtx_lock(&g_dmDevicesLock);

    device = DmDeviceFindLocked(deviceId);
    if (device == NULL) {
        TRACE("DmDeviceRequestAcquire: device %u was not found", deviceId);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_ENOENT;
    }

    if (device->Removing) {
        TRACE("DmDeviceRequestAcquire: device %u is being removed", deviceId);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_EBUSY;
    }

    // Stop before ActiveRequests wraps back to zero, which would let removal
    // free the device while a callback is still running.
    if (device->ActiveRequests == UINT_MAX) {
        WARNING("DmDeviceRequestAcquire: active request count overflow for device %u", deviceId);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_EOVERFLOW;
    }
    device->ActiveRequests++;

    *deviceOut = device;
    usched_mtx_unlock(&g_dmDevicesLock);
    return OS_EOK;
}

void
DmDeviceRequestRelease(
    _In_ DmDevice_t* device)
{
    usched_mtx_lock(&g_dmDevicesLock);
    device->ActiveRequests--;
    usched_mtx_unlock(&g_dmDevicesLock);
}

oserr_t
DmHandleIoctl(
    _In_ uuid_t              principal,
    _In_ uuid_t              deviceId,
    _In_ enum OSIOCtlRequest request,
    _In_ void*               buffer,
    _In_ size_t              length)
{
    DmDevice_t*                    device;
    const DmDeviceBusOperations_t* operations;
    oserr_t                        status;
    
    TRACE("DmHandleIoctl(principal=%u, device=%u, request=0x%x, length=%zu)",
          principal, deviceId, request, length);

    if (request == OSIOCTLREQUEST_DMA_QUERY || request == OSIOCTLREQUEST_DMA_RELEASE) {
        return DmDmaControl(principal, deviceId, request, buffer, length);
    }

    status = DmDeviceRequestAcquire(deviceId, &device);
    if (status != OS_EOK) {
        WARNING("DmHandleIoctl: could not acquire device %u for request 0x%x: %u",
                deviceId, request, status);
        return status;
    }
    operations = device->Bus.Operations;

    if (request == OSIOCTLREQUEST_BUS_CONTROL &&
        (buffer == NULL || length < sizeof(struct OSIOCtlBusControl))) {
        WARNING("DmHandleIoctl: invalid bus-control buffer for device %u, length %zu", deviceId, length);
        status = OS_EINVALPARAMS;
    } else if (operations != NULL && operations->Control != NULL) {
        status = operations->Control(
            device->Bus.Context,
            principal,
            request,
            buffer,
            length
        );
        if (status != OS_EOK) {
            WARNING("DmHandleIoctl: bus failed request 0x%x for device %u, principal %u: %u",
                    request, deviceId, principal, status);
        }
    } else {
        TRACE("DmHandleIoctl: device %u has no bus control callback for request 0x%x", deviceId, request);
        status = OS_ENOTSUPPORTED;
    }

    DmDeviceRequestRelease(device);
    return status;
}

oserr_t
DmHandleIoctl2(
    _In_  uuid_t       deviceId,
    _In_  int          direction,
    _In_  unsigned int command,
    _In_  size_t       value,
    _In_  unsigned int width,
    _Out_ size_t*      valueOut)
{
    DmDevice_t*                    device;
    const DmDeviceBusOperations_t* operations;
    oserr_t                        status;
    TRACE("DmHandleIoctl2(device=%u, direction=%i, command=0x%x, width=%u)",
          deviceId, direction, command, width);

    *valueOut = value;
    status = DmDeviceRequestAcquire(deviceId, &device);
    if (status != OS_EOK) {
        WARNING("DmHandleIoctl2: could not acquire device %u for command 0x%x: %u",
                deviceId, command, status);
        return status;
    }

    operations = device->Bus.Operations;
    if (operations == NULL || operations->AccessRegister == NULL) {
        TRACE("DmHandleIoctl2: device %u has no register bus", deviceId);
        status = OS_ENOTSUPPORTED;
    } else {
        status = operations->AccessRegister(
            device->Bus.Context,
            direction,
            command,
            valueOut,
            width
        );
        if (status != OS_EOK) {
            WARNING("DmHandleIoctl2: bus failed command 0x%x for device %u: %u",
                    command, deviceId, status);
        }
    }

    DmDeviceRequestRelease(device);
    return status;
}

// Requires g_dmDevicesLock and a device that is still accepting protocols.
static oserr_t
__AddProtocolToDevice(
    _In_ const char*        protocolName,
    _In_ uint8_t            protocolID,
    _In_ DmDevice_t* device)
{
    struct __DmDeviceProtocol* protocol;

    // Protocol IDs must be unique within a device.
    foreach (node, &device->Protocols) {
        protocol = node->value;
        if ((uint8_t)(uintptr_t)protocol->Header.key == protocolID) {
            return OS_EEXISTS;
        }
    }

    protocol = malloc(sizeof(*protocol));
    if (protocol == NULL) {
        ERROR("__AddProtocolToDevice: failed to allocate protocol %u for device %u",
              protocolID, device->Device->Id);
        return OS_EOOM;
    }

    protocol->Name = strdup(protocolName);
    if (protocol->Name == NULL) {
        ERROR("__AddProtocolToDevice: failed to copy protocol %u name for device %u",
              protocolID, device->Device->Id);
        free(protocol);
        return OS_EOOM;
    }

    ELEMENT_INIT(&protocol->Header, (uintptr_t)protocolID, protocol);
    list_append(&device->Protocols, &protocol->Header);
    return OS_EOK;
}

void
DmHandleRegisterProtocol(
    _In_ uuid_t      deviceID,
    _In_ const char* protocolName,
    _In_ uint8_t     protocolID)
{
    DmDevice_t* device;
    oserr_t            oserr;
    int                status;
    TRACE("DmHandleRegisterProtocol(device=%u, protocol=%u, name=%s)",
          deviceID, protocolID, protocolName);

    usched_mtx_lock(&g_dmDevicesLock);

    device = DmDeviceFindLocked(deviceID);
    if (device == NULL || device->Removing || device->DriverId == UUID_INVALID) {
        TRACE("DmHandleRegisterProtocol: device %u has no active driver", deviceID);
        usched_mtx_unlock(&g_dmDevicesLock);
        return;
    }

    oserr = __AddProtocolToDevice(protocolName, protocolID, device);
    if (oserr != OS_EOK) {
        if (oserr == OS_EEXISTS) {
            TRACE("DmHandleRegisterProtocol: device %u already has protocol %u", deviceID, protocolID);
        } else {
            WARNING("DmHandleRegisterProtocol: adding protocol %u to device %u failed: %u",
                    protocolID, deviceID, oserr);
        }
        usched_mtx_unlock(&g_dmDevicesLock);
        return;
    }

    // Subscribers may have queried before this driver finished attaching.
    status = sys_device_event_protocol_device_all(
        __crt_get_service_server(),
        deviceID,
        device->DriverId,
        protocolID
    );
    if (status != 0) {
        WARNING("DmHandleRegisterProtocol: broadcasting device %u protocol %u failed: %i",
                deviceID, protocolID, status);
    }
    usched_mtx_unlock(&g_dmDevicesLock);
}

static void
__TryLocateDriver(
    _In_ uuid_t deviceId)
{
    DmDevice_t*                 device;
    struct DriverIdentification identification = { 0 };
    PlatformDevice_t*           platform;
    char                        compatibles[PLATFORM_DEVICE_MAX_COMPATIBLES];
    oserr_t                     status;
    TRACE("__TryLocateDriver(device=%u)", deviceId);

    usched_mtx_lock(&g_dmDevicesLock);
    device = DmDeviceFindLocked(deviceId);
    if (device == NULL || device->Removing) {
        usched_mtx_unlock(&g_dmDevicesLock);
        return;
    }
    if (device->HasDriver || !(device->Flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
        usched_mtx_unlock(&g_dmDevicesLock);
        return;
    }

    identification.VendorId = device->Device->VendorId;
    identification.ProductId = device->Device->ProductId;
    identification.Class = device->Device->Class;
    identification.Subclass = device->Device->Subclass;

    if (device->Kind == DmDeviceDescriptionPlatform) {
        platform = (PlatformDevice_t*)device->Device;
        memcpy(compatibles, platform->Compatibles, platform->CompatibleLength);
        identification.IsPlatform = 1;
        identification.Compatibles = compatibles;
        identification.CompatibleLength = platform->CompatibleLength;
    }

    // Reserve this attempt, then release the registry lock. A ready driver can
    // call back into DmDevicesRegister, which acquires that same lock.
    device->HasDriver = true;
    usched_mtx_unlock(&g_dmDevicesLock);

    status = DmDiscoverFindDriver(deviceId, &identification);
    if (status != OS_EOK) {
        if (status == OS_ENOENT) {
            TRACE("__TryLocateDriver: no driver available for device %u", deviceId);
        } else {
            WARNING("__TryLocateDriver: binding device %u failed: %u", deviceId, status);
        }
        usched_mtx_lock(&g_dmDevicesLock);
        device = DmDeviceFindLocked(deviceId);
        if (device != NULL && device->DriverId == UUID_INVALID) {
            device->HasDriver = false;
        }
        usched_mtx_unlock(&g_dmDevicesLock);
    }
}

int
DmDeviceIsBindable(
    _In_ uuid_t deviceId)
{
    DmDevice_t* device;
    int                bindable;

    usched_mtx_lock(&g_dmDevicesLock);

    device = DmDeviceFindLocked(deviceId);
    bindable = device != NULL && !device->Removing &&
        (device->Flags & DEVICE_REGISTER_FLAG_LOADDRIVER);

    usched_mtx_unlock(&g_dmDevicesLock);
    return bindable;
}

oserr_t
DmDeviceEnableDriverBinding(
    _In_ uuid_t deviceId)
{
    DmDevice_t* device;
    TRACE("DmDeviceEnableDriverBinding(device=%u)", deviceId);

    usched_mtx_lock(&g_dmDevicesLock);

    device = DmDeviceFindLocked(deviceId);
    if (device == NULL) {
        TRACE("DmDeviceEnableDriverBinding: device %u was not found", deviceId);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_ENOENT;
    }

    if (device->Removing) {
        WARNING("DmDeviceEnableDriverBinding: device %u is being removed", deviceId);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_EBUSY;
    }

    device->Flags |= DEVICE_REGISTER_FLAG_LOADDRIVER;
    usched_mtx_unlock(&g_dmDevicesLock);

#ifndef __OSCONFIG_NODRIVERS
    __TryLocateDriver(deviceId);
#endif
    return OS_EOK;
}

static int
__ValidDeviceRegistration(
    _In_ const DmDeviceRegistration_t* registration)
{
    const Device_t*                description = registration->Description;
    const DmDeviceBusOperations_t* operations = registration->Bus.Operations;

    if (description == NULL) {
        WARNING("__ValidDeviceRegistration: missing description for kind %i", registration->Kind);
        return 0;
    }

    if (operations != NULL) {
        if (operations->AddReference == NULL) {
            WARNING("__ValidDeviceRegistration: kind %i has no AddReference callback",
                    registration->Kind);
            return 0;
        }
        if (operations->Release == NULL) {
            WARNING("__ValidDeviceRegistration: kind %i has no Release callback",
                    registration->Kind);
            return 0;
        }
    } else if (registration->Bus.Context != NULL) {
        WARNING("__ValidDeviceRegistration: kind %i has a context without operations",
                registration->Kind);
        return 0;
    }

    switch (registration->Kind) {
        case DmDeviceDescriptionGeneric:
            if (description->Length != sizeof(Device_t)) {
                WARNING("__ValidDeviceRegistration: generic description length %zu, expected %zu",
                        (size_t)description->Length, sizeof(Device_t));
                return 0;
            }
            return 1;
        case DmDeviceDescriptionBus:
            if (description->Length != sizeof(BusDevice_t)) {
                WARNING("__ValidDeviceRegistration: bus description length %zu, expected %zu",
                        (size_t)description->Length, sizeof(BusDevice_t));
                return 0;
            }
            return 1;
        case DmDeviceDescriptionUsb:
            if (description->Length != sizeof(UsbDevice_t)) {
                WARNING("__ValidDeviceRegistration: USB description length %zu, expected %zu",
                        (size_t)description->Length, sizeof(UsbDevice_t));
                return 0;
            }
            return 1;
        case DmDeviceDescriptionPlatform:
            if (description->Length != sizeof(PlatformDevice_t)) {
                WARNING("__ValidDeviceRegistration: platform description length %zu, expected %zu",
                        (size_t)description->Length, sizeof(PlatformDevice_t));
                return 0;
            }
            if (!PlatformDeviceValidate((const PlatformDevice_t*)description)) {
                WARNING("__ValidDeviceRegistration: platform description for parent %u is invalid",
                        description->ParentId);
                return 0;
            }
            return 1;
        default:
            WARNING("__ValidDeviceRegistration: unsupported description kind %i", registration->Kind);
            return 0;
    }
}

oserr_t
DmDeviceCreateWithBus(
    _In_  const DmDeviceRegistration_t* registration,
    _In_  unsigned int                  flags,
    _Out_ uuid_t*                       idOut)
{
    DmDevice_t* deviceNode;
    DmDevice_t* parent;
    Device_t*          description;
    oserr_t            status;
    TRACE("DmDeviceCreateWithBus(kind=%i, flags=0x%x)", registration->Kind, flags);

    *idOut = UUID_INVALID;

    if (!__ValidDeviceRegistration(registration)) {
        return OS_EINVALPARAMS;
    }

    deviceNode = calloc(1, sizeof(*deviceNode));
    if (deviceNode == NULL) {
        ERROR("DmDeviceCreateWithBus: failed to allocate device registry entry");
        return OS_EOOM;
    }

    deviceNode->Bus = registration->Bus;
    if (deviceNode->Bus.Operations != NULL) {
        status = deviceNode->Bus.Operations->AddReference(deviceNode->Bus.Context);
        if (status != OS_EOK) {
            WARNING("DmDeviceCreateWithBus: failed to add bus reference: %u", status);
            free(deviceNode);
            return status;
        }
    }

    description = registration->Description;
    ELEMENT_INIT(&deviceNode->Header, 0, deviceNode);
    deviceNode->DriverId = UUID_INVALID;
    deviceNode->DriverPrincipal = 0;
    deviceNode->Device = description;
    deviceNode->Kind = registration->Kind;
    deviceNode->Flags = flags;
    list_construct(&deviceNode->Protocols);

    usched_mtx_lock(&g_dmDevicesLock);
    if (description->ParentId != UUID_INVALID) {
        parent = DmDeviceFindLocked(description->ParentId);
        if (parent == NULL || parent->Removing) {
            WARNING("DmDeviceCreateWithBus: parent %u is absent or being removed",
                    description->ParentId);
            usched_mtx_unlock(&g_dmDevicesLock);
            if (deviceNode->Bus.Operations != NULL) {
                deviceNode->Bus.Operations->Release(deviceNode->Bus.Context);
            }
            free(deviceNode);
            return OS_ENOENT;
        }
    }

    description->Id = g_nextDeviceId++;
    deviceNode->Header.key = (void*)(uintptr_t)description->Id;
    // Once added to the list, the registry is responsible for freeing the
    // description and its allocated strings.
    list_append(&g_devices, &deviceNode->Header);

    *idOut = description->Id;
    TRACE("DmDeviceCreateWithBus: registered device %u, parent %u, kind %i",
          description->Id, description->ParentId, registration->Kind);
    usched_mtx_unlock(&g_dmDevicesLock);

#ifndef __OSCONFIG_NODRIVERS
    if (flags & DEVICE_REGISTER_FLAG_LOADDRIVER) {
        __TryLocateDriver(*idOut);
    }
#endif
    return OS_EOK;
}

oserr_t
DmDeviceCreate(
    _In_  Device_t*    device,
    _In_  unsigned int flags,
    _Out_ uuid_t*      idOut)
{
    DmDeviceRegistration_t registration = {
        .Description = device,
        .Kind = DmDeviceDescriptionGeneric
    };
    oserr_t status;
    TRACE("DmDeviceCreate(flags=0x%x)", flags);

    // Older callers provide only the description. Registering a bus address
    // without callbacks gives them no access to bus operations.
    if (device != NULL) {
        if (device->Length == sizeof(BusDevice_t)) {
            registration.Kind = DmDeviceDescriptionBus;
        } else if (device->Length == sizeof(PlatformDevice_t)) {
            registration.Kind = DmDeviceDescriptionPlatform;
        } else if (device->Length == sizeof(UsbDevice_t)) {
            registration.Kind = DmDeviceDescriptionUsb;
        }
    }
    status = DmDeviceCreateWithBus(&registration, flags, idOut);
    if (status != OS_EOK) {
        WARNING("DmDeviceCreate: registration failed: %u", status);
        return status;
    }
    return OS_EOK;
}

void
DmDeviceRefreshDrivers(void)
{
#ifndef __OSCONFIG_NODRIVERS
    DmDevice_t* device;
    uuid_t*            ids;
    size_t             count = 0;
    size_t             index;
    TRACE("DmDeviceRefreshDrivers()");

    // Driver discovery may call back into the registry. Copy IDs under the
    // lock and let __TryLocateDriver check each entry again after unlocking.
    usched_mtx_lock(&g_dmDevicesLock);
    ids = malloc(g_devices.count * sizeof(*ids));
    if (ids == NULL) {
        WARNING("DmDeviceRefreshDrivers: failed to allocate device ID snapshot");
        usched_mtx_unlock(&g_dmDevicesLock);
        TRACE("DmDeviceRefreshDrivers: refreshing %zu devices", count);
        return;
    }
    foreach (i, &g_devices) {
        device = i->value;
        if (!device->Removing && !device->HasDriver && (device->Flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
            ids[count++] = device->Device->Id;
        }
    }
    usched_mtx_unlock(&g_dmDevicesLock);
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
    DmDevice_t*                device;
    struct __DmDeviceProtocol* protocol;
    DeviceIdentification_t*    identification;
    TRACE("DmDeviceDestroy(device=%u)", deviceId);

    usched_mtx_lock(&g_dmDevicesLock);

    device = DmDeviceFindLocked(deviceId);
    if (device == NULL) {
        TRACE("DmDeviceDestroy: device %u was not found", deviceId);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_ENOENT;
    }

    // Stop new users before checking existing ones. On OS_EBUSY, the caller
    // must wait for requests, release DMA leases and remove children, then retry.
    device->Removing = 1;

    if (device->ActiveRequests != 0 || device->DmaLeases != 0) {
        WARNING("DmDeviceDestroy: device %u still has %u requests and %u DMA leases",
                deviceId, device->ActiveRequests, device->DmaLeases);
        usched_mtx_unlock(&g_dmDevicesLock);
        return OS_EBUSY;
    }

    foreach (i, &g_devices) {
        if (((DmDevice_t*)i->value)->Device->ParentId == deviceId) {
            WARNING("DmDeviceDestroy: device %u still has child %u",
                    deviceId, ((DmDevice_t*)i->value)->Device->Id);
            usched_mtx_unlock(&g_dmDevicesLock);
            return OS_EBUSY;
        }
    }

    list_remove(&g_devices, &device->Header);
    usched_mtx_unlock(&g_dmDevicesLock);

    // Do not hold the registry lock while cancelling discovery work. A driver
    // completing startup must see either the live device or ENOENT.
    DmDiscoverForgetDevice(deviceId);

    while (device->Protocols.head != NULL) {
        protocol = device->Protocols.head->value;
        list_remove(&device->Protocols, &protocol->Header);
        free(protocol->Name);
        free(protocol);
    }

    if (device->Bus.Operations != NULL) {
        device->Bus.Operations->Release(device->Bus.Context);
    }

    identification = &device->Device->Identification;

    free(identification->Description);
    free(identification->Manufacturer);
    free(identification->Product);
    free(identification->Revision);
    free(identification->Serial);
    free(device->Device);
    free(device);
    TRACE("DmDeviceDestroy: removed device %u", deviceId);
    return OS_EOK;
}
