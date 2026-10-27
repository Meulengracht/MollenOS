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

struct __DmDevice {
    element_t                    Header;
    uuid_t                       DriverId;
    bool                         HasDriver; // A driver search is pending or succeeded.
    unsigned int                 Flags;
    Device_t*                    Device;
    list_t                       Protocols; // list<struct __DmDeviceProtocol>
    enum DmDeviceDescriptionKind Kind;
    struct DmDeviceProvider      Provider;
    unsigned int                 ActiveRequests;
    unsigned int                 DmaLeases;
    int                          Removing;
};

/**
 * @brief A device reference and a copy of its configured DMA description.
 * The lease prevents device destruction until released. It has no timeout.
 */
struct DmDmaLease {
    struct __DmDevice*      Device;
    struct DmDmaDescription Description;
};

static struct usched_mtx g_devicesLock;
static list_t            g_devices      = LIST_INIT;
static uuid_t            g_nextDeviceId = 1;

void
DmDevicesInitialize(void)
{
    TRACE("DmDevicesInitialize()");
    usched_mtx_init(&g_devicesLock, USCHED_MUTEX_PLAIN);
    DmDmaSessionsInitialize();
}

// Requires g_devicesLock. The returned entry is valid while that lock is held
// or while a request or DMA lease prevents destruction.
static struct __DmDevice*
__GetDeviceUnsafe(
    _In_ uuid_t deviceId)
{
    struct __DmDevice* result = NULL;
    struct __DmDevice* device;

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
    _In_ uuid_t driverHandle,
    _In_ uuid_t deviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverHandle);
    struct __DmDevice*       device;
    struct sys_device        protoDevice;
    oserr_t                  status;
    int                      sent;
    TRACE("DmDevicesRegister(driver=%u, device=%u)", driverHandle, deviceId);

    // Copy the description under the registry lock. Sending a driver message
    // may yield, so the message must use its own copy after the lock is released.
    usched_mtx_lock(&g_devicesLock);

    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL || device->Removing || !(device->Flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
        TRACE("DmDevicesRegister: device %u is not available for binding", deviceId);
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }

    if (device->DriverId != UUID_INVALID) {
        TRACE("DmDevicesRegister: device %u already has driver %u", deviceId, device->DriverId);
        usched_mtx_unlock(&g_devicesLock);
        return OS_EEXISTS;
    }

    status = to_sys_device(device->Device, &protoDevice);
    if (status != OS_EOK) {
        ERROR("DmDevicesRegister: failed to serialize device %u for driver %u: %u",
              deviceId, driverHandle, status);
        device->HasDriver = false;
        usched_mtx_unlock(&g_devicesLock);
        return status;
    }

    device->DriverId = driverHandle;
    usched_mtx_unlock(&g_devicesLock);

    sent = ctt_driver_register_device(GetGrachtClient(), &msg.base, &protoDevice);
    if (sent != 0) {
        WARNING("DmDevicesRegister: sending device %u to driver %u failed: %i",
                deviceId, driverHandle, sent);
        sys_device_destroy(&protoDevice);
        usched_mtx_lock(&g_devicesLock);
        device = __GetDeviceUnsafe(deviceId);
        if (device != NULL) {
            device->DriverId = UUID_INVALID;
            device->HasDriver = false;
        }
        usched_mtx_unlock(&g_devicesLock);
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

    usched_mtx_lock(&g_devicesLock);
    foreach (node, &g_devices) {
        struct __DmDevice* device = node->value;

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

    usched_mtx_unlock(&g_devicesLock);
}

// On success, release the request with __ReleaseDeviceRequest or replace it
// with a counted DMA lease. The count protects the entry without g_devicesLock.
static oserr_t
__AcquireDeviceRequest(
    _In_  uuid_t              deviceId,
    _Out_ struct __DmDevice** deviceOut)
{
    struct __DmDevice* device;

    usched_mtx_lock(&g_devicesLock);

    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL) {
        TRACE("__AcquireDeviceRequest: device %u was not found", deviceId);
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }

    if (device->Removing) {
        TRACE("__AcquireDeviceRequest: device %u is being removed", deviceId);
        usched_mtx_unlock(&g_devicesLock);
        return OS_EBUSY;
    }

    // Wrapping the request count could allow removal during a live callback.
    if (device->ActiveRequests == UINT_MAX) {
        WARNING("__AcquireDeviceRequest: active request count overflow for device %u", deviceId);
        usched_mtx_unlock(&g_devicesLock);
        return OS_EOVERFLOW;
    }
    device->ActiveRequests++;

    *deviceOut = device;
    usched_mtx_unlock(&g_devicesLock);
    return OS_EOK;
}

static void
__ReleaseDeviceRequest(
    _In_ struct __DmDevice* device)
{
    usched_mtx_lock(&g_devicesLock);
    device->ActiveRequests--;
    usched_mtx_unlock(&g_devicesLock);
}

static oserr_t
__DevicePrepareDma(
    _In_    uuid_t              deviceId,
    _In_    uuid_t              principal,
    _InOut_ struct DmDmaLease** leaseOut)
{
    struct __DmDevice*                       device;
    const struct DmDeviceProviderOperations* operations;
    struct DmDmaLease*                       lease;
    oserr_t                                  status;
    TRACE("DmDevicePrepareDma(device=%u)", deviceId);

    // Require an empty output slot so preparing twice cannot lose an earlier
    // lease and permanently prevent the device from being removed.
    if (leaseOut == NULL) {
        WARNING("DmDevicePrepareDma: missing lease output for device %u", deviceId);
        return OS_EINVALPARAMS;
    }
    if (*leaseOut != NULL) {
        WARNING("DmDevicePrepareDma: lease output for device %u is occupied", deviceId);
        return OS_EBUSY;
    }

    status = __AcquireDeviceRequest(deviceId, &device);
    if (status != OS_EOK) {
        WARNING("DmDevicePrepareDma: could not acquire device %u: %u", deviceId, status);
        return status;
    }

    operations = device->Provider.Operations;
    if (operations == NULL || operations->PrepareDma == NULL) {
        TRACE("DmDevicePrepareDma: device %u has no DMA provider", deviceId);
        __ReleaseDeviceRequest(device);
        return OS_ENOTSUPPORTED;
    }

    lease = calloc(1, sizeof(struct DmDmaLease));
    if (lease == NULL) {
        ERROR("DmDevicePrepareDma: failed to allocate lease for device %u", deviceId);
        __ReleaseDeviceRequest(device);
        return OS_EOOM;
    }

    // The request count keeps the entry and its callback context alive.
    // Bus callbacks may take their own locks or fail. Run them without the
    // registry lock and keep their result private until the lease is counted.
    status = operations->PrepareDma(
        device->Provider.Context,
        principal,
        &lease->Description
    );
    if (status != OS_EOK) {
        WARNING("DmDevicePrepareDma: provider for device %u failed: %u", deviceId, status);
        __ReleaseDeviceRequest(device);
        free(lease);
        return status;
    }

    // Count the lease before releasing the request. If removal started during
    // the callback, undo DMA preparation and leave the output slot empty.
    usched_mtx_lock(&g_devicesLock);
    if (device->Removing) {
        WARNING("DmDevicePrepareDma: device %u removal started during preparation", deviceId);
        status = OS_EBUSY;
    } else if (device->DmaLeases == UINT_MAX) {
        WARNING("DmDevicePrepareDma: lease count overflow for device %u", deviceId);
        status = OS_EOVERFLOW;
    } else {
        device->DmaLeases++;
        lease->Device = device;
        lease->Description.DeviceId = deviceId;
        device->ActiveRequests--;
    }
    usched_mtx_unlock(&g_devicesLock);

    if (status != OS_EOK) {
        // Keep the request count until DMA cleanup finishes. Otherwise
        // concurrent removal could free its context before ReleaseDma runs.
        if (operations->ReleaseDma != NULL) {
            operations->ReleaseDma(device->Provider.Context);
        }

        __ReleaseDeviceRequest(device);
        free(lease);
        return status;
    }

    *leaseOut = lease;
    TRACE("DmDevicePrepareDma: committed lease for device %u", deviceId);
    return OS_EOK;
}

oserr_t
DmDevicePrepareDma(
    _In_    uuid_t              deviceId,
    _InOut_ struct DmDmaLease** leaseOut)
{
    return __DevicePrepareDma(deviceId, UUID_INVALID, leaseOut);
}

oserr_t
DmDevicePrepareDmaForPrincipal(
    _In_    uuid_t              deviceId,
    _In_    uuid_t              principal,
    _InOut_ struct DmDmaLease** leaseOut)
{
    if (principal == UUID_INVALID) {
        return OS_EPERMISSIONS;
    }
    return __DevicePrepareDma(deviceId, principal, leaseOut);
}

const struct DmDmaDescription*
DmDmaLeaseGetDescription(
    _In_ const struct DmDmaLease* lease)
{
    // The copy remains valid until this lease is released.
    return lease == NULL ? NULL : &lease->Description;
}

void
DmDmaLeaseRelease(
    _InOut_ struct DmDmaLease** lease)
{
    struct DmDmaLease* owned;

    // Clearing the caller's slot makes repeated cleanup on that slot harmless.
    // No request lookup is needed: releasing must work after removal starts.
    if (lease == NULL || *lease == NULL) {
        return;
    }

    owned = *lease;
    *lease = NULL;

    // The outstanding lease still prevents registry removal during this call.
    if (owned->Device->Provider.Operations->ReleaseDma != NULL) {
        owned->Device->Provider.Operations->ReleaseDma(owned->Device->Provider.Context);
    }

    usched_mtx_lock(&g_devicesLock);
    TRACE("DmDmaLeaseRelease: releasing lease for device %u", owned->Device->Device->Id);
    owned->Device->DmaLeases--;
    usched_mtx_unlock(&g_devicesLock);

    // The entry may now be destroyed by another thread. Touch only our copy.
    free(owned);
}

oserr_t
DmHandleIoctl(
    _In_ uuid_t              principal,
    _In_ uuid_t              deviceId,
    _In_ enum OSIOCtlRequest request,
    _In_ void*               buffer,
    _In_ size_t              length)
{
    struct __DmDevice*                       device;
    const struct DmDeviceProviderOperations* operations;
    uuid_t                                   driverId;
    oserr_t                                  status;
    TRACE("DmHandleIoctl(principal=%u, device=%u, request=0x%x, length=%zu)",
          principal, deviceId, request, length);

    if (request == OSIOCTLREQUEST_DMA_QUERY || request == OSIOCTLREQUEST_DMA_RELEASE) {
        return DmDmaControl(principal, deviceId, request, buffer, length);
    }

    status = __AcquireDeviceRequest(deviceId, &device);
    if (status != OS_EOK) {
        WARNING("DmHandleIoctl: could not acquire device %u for request 0x%x: %u",
                deviceId, request, status);
        return status;
    }
    operations = device->Provider.Operations;

    // Driver selection may change independently of an active request.
    usched_mtx_lock(&g_devicesLock);
    driverId = device->DriverId;
    usched_mtx_unlock(&g_devicesLock);

    if (request == OSIOCTLREQUEST_BUS_CONTROL &&
        (buffer == NULL || length < sizeof(struct OSIOCtlBusControl))) {
        WARNING("DmHandleIoctl: invalid bus-control buffer for device %u, length %zu", deviceId, length);
        status = OS_EINVALPARAMS;
    } else if (request == OSIOCTLREQUEST_IO_REQUIREMENTS && driverId != UUID_INVALID) {
        status = OSDeviceIOCtl2(deviceId, driverId, request, buffer, length);
        if (status != OS_EOK) {
            WARNING("DmHandleIoctl: driver %u failed request 0x%x for device %u: %u",
                    driverId, request, deviceId, status);
        }
    } else if (operations != NULL && operations->Control != NULL) {
        status = operations->Control(
            device->Provider.Context,
            principal,
            request,
            buffer,
            length
        );
        if (status != OS_EOK) {
            WARNING("DmHandleIoctl: provider failed request 0x%x for device %u, principal %u: %u",
                    request, deviceId, principal, status);
        }
    } else {
        TRACE("DmHandleIoctl: device %u has no provider for request 0x%x", deviceId, request);
        status = OS_ENOTSUPPORTED;
    }

    __ReleaseDeviceRequest(device);
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
    struct __DmDevice*                       device;
    const struct DmDeviceProviderOperations* operations;
    oserr_t                                  status;
    TRACE("DmHandleIoctl2(device=%u, direction=%i, command=0x%x, width=%u)",
          deviceId, direction, command, width);

    *valueOut = value;
    status = __AcquireDeviceRequest(deviceId, &device);
    if (status != OS_EOK) {
        WARNING("DmHandleIoctl2: could not acquire device %u for command 0x%x: %u",
                deviceId, command, status);
        return status;
    }

    operations = device->Provider.Operations;
    if (operations == NULL || operations->AccessRegister == NULL) {
        TRACE("DmHandleIoctl2: device %u has no register provider", deviceId);
        status = OS_ENOTSUPPORTED;
    } else {
        status = operations->AccessRegister(
            device->Provider.Context,
            direction,
            command,
            valueOut,
            width
        );
        if (status != OS_EOK) {
            WARNING("DmHandleIoctl2: provider failed command 0x%x for device %u: %u",
                    command, deviceId, status);
        }
    }

    __ReleaseDeviceRequest(device);
    return status;
}

// Requires g_devicesLock and a device that is still accepting protocols.
static oserr_t
__AddProtocolToDevice(
    _In_ const char*        protocolName,
    _In_ uint8_t            protocolID,
    _In_ struct __DmDevice* device)
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
    struct __DmDevice* device;
    oserr_t            oserr;
    int                status;
    TRACE("DmHandleRegisterProtocol(device=%u, protocol=%u, name=%s)",
          deviceID, protocolID, protocolName);

    usched_mtx_lock(&g_devicesLock);

    device = __GetDeviceUnsafe(deviceID);
    if (device == NULL || device->Removing || device->DriverId == UUID_INVALID) {
        TRACE("DmHandleRegisterProtocol: device %u has no active driver", deviceID);
        usched_mtx_unlock(&g_devicesLock);
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
        usched_mtx_unlock(&g_devicesLock);
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
    usched_mtx_unlock(&g_devicesLock);
}

static void
__TryLocateDriver(
    _In_ uuid_t deviceId)
{
    struct __DmDevice*          device;
    struct DriverIdentification identification = { 0 };
    PlatformDevice_t*           platform;
    char                        compatibles[PLATFORM_DEVICE_MAX_COMPATIBLES];
    oserr_t                     status;
    TRACE("__TryLocateDriver(device=%u)", deviceId);

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL || device->Removing) {
        usched_mtx_unlock(&g_devicesLock);
        return;
    }
    if (device->HasDriver || !(device->Flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
        usched_mtx_unlock(&g_devicesLock);
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
    usched_mtx_unlock(&g_devicesLock);

    status = DmDiscoverFindDriver(deviceId, &identification);
    if (status != OS_EOK) {
        if (status == OS_ENOENT) {
            TRACE("__TryLocateDriver: no driver available for device %u", deviceId);
        } else {
            WARNING("__TryLocateDriver: binding device %u failed: %u", deviceId, status);
        }
        usched_mtx_lock(&g_devicesLock);
        device = __GetDeviceUnsafe(deviceId);
        if (device != NULL && device->DriverId == UUID_INVALID) {
            device->HasDriver = false;
        }
        usched_mtx_unlock(&g_devicesLock);
    }
}

int
DmDeviceIsBindable(
    _In_ uuid_t deviceId)
{
    struct __DmDevice* device;
    int                bindable;

    usched_mtx_lock(&g_devicesLock);

    device = __GetDeviceUnsafe(deviceId);
    bindable = device != NULL && !device->Removing &&
        (device->Flags & DEVICE_REGISTER_FLAG_LOADDRIVER);

    usched_mtx_unlock(&g_devicesLock);
    return bindable;
}

oserr_t
DmDeviceEnableDriverBinding(
    _In_ uuid_t deviceId)
{
    struct __DmDevice* device;
    TRACE("DmDeviceEnableDriverBinding(device=%u)", deviceId);

    usched_mtx_lock(&g_devicesLock);

    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL) {
        TRACE("DmDeviceEnableDriverBinding: device %u was not found", deviceId);
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }

    if (device->Removing) {
        WARNING("DmDeviceEnableDriverBinding: device %u is being removed", deviceId);
        usched_mtx_unlock(&g_devicesLock);
        return OS_EBUSY;
    }

    device->Flags |= DEVICE_REGISTER_FLAG_LOADDRIVER;
    usched_mtx_unlock(&g_devicesLock);

#ifndef __OSCONFIG_NODRIVERS
    __TryLocateDriver(deviceId);
#endif
    return OS_EOK;
}

static int
__ValidDeviceRegistration(
    _In_ const struct DmDeviceRegistration* registration)
{
    const Device_t*                          description = registration->Description;
    const struct DmDeviceProviderOperations* operations = registration->Provider.Operations;

    if (description == NULL) {
        return 0;
    }

    if (operations != NULL) {
        if (operations->AddReference == NULL || operations->Release == NULL) {
            return 0;
        }
    } else if (registration->Provider.Context != NULL) {
        return 0;
    }

    switch (registration->Kind) {
        case DmDeviceDescriptionGeneric:
            return description->Length == sizeof(Device_t);
        case DmDeviceDescriptionBus:
            return description->Length == sizeof(BusDevice_t);
        case DmDeviceDescriptionUsb:
            return description->Length == sizeof(UsbDevice_t);
        case DmDeviceDescriptionPlatform:
            return description->Length == sizeof(PlatformDevice_t) &&
                PlatformDeviceValidate((const PlatformDevice_t*)description);
        default:
            return 0;
    }
}

oserr_t
DmDeviceCreateWithProvider(
    _In_  const struct DmDeviceRegistration* registration,
    _In_  unsigned int                      flags,
    _Out_ uuid_t*                           idOut)
{
    struct __DmDevice* deviceNode;
    struct __DmDevice* parent;
    Device_t*          description;
    oserr_t            status;
    TRACE("DmDeviceCreateWithProvider(kind=%i, flags=0x%x)", registration->Kind, flags);

    *idOut = UUID_INVALID;

    if (!__ValidDeviceRegistration(registration)) {
        WARNING("DmDeviceCreateWithProvider: invalid registration for description kind %i",
                registration->Kind);
        return OS_EINVALPARAMS;
    }

    deviceNode = calloc(1, sizeof(*deviceNode));
    if (deviceNode == NULL) {
        ERROR("DmDeviceCreateWithProvider: failed to allocate device registry entry");
        return OS_EOOM;
    }

    deviceNode->Provider = registration->Provider;
    if (deviceNode->Provider.Operations != NULL) {
        status = deviceNode->Provider.Operations->AddReference(deviceNode->Provider.Context);
        if (status != OS_EOK) {
            WARNING("DmDeviceCreateWithProvider: failed to add provider reference: %u", status);
            free(deviceNode);
            return status;
        }
    }

    description = registration->Description;
    ELEMENT_INIT(&deviceNode->Header, 0, deviceNode);
    deviceNode->DriverId = UUID_INVALID;
    deviceNode->Device = description;
    deviceNode->Kind = registration->Kind;
    deviceNode->Flags = flags;
    list_construct(&deviceNode->Protocols);

    usched_mtx_lock(&g_devicesLock);
    if (description->ParentId != UUID_INVALID) {
        parent = __GetDeviceUnsafe(description->ParentId);
        if (parent == NULL || parent->Removing) {
            WARNING("DmDeviceCreateWithProvider: parent %u is absent or being removed",
                    description->ParentId);
            usched_mtx_unlock(&g_devicesLock);
            if (deviceNode->Provider.Operations != NULL) {
                deviceNode->Provider.Operations->Release(deviceNode->Provider.Context);
            }
            free(deviceNode);
            return OS_ENOENT;
        }
    }

    description->Id = g_nextDeviceId++;
    deviceNode->Header.key = (void*)(uintptr_t)description->Id;
    // Once listed, the registry owns the description and its allocated strings.
    list_append(&g_devices, &deviceNode->Header);

    *idOut = description->Id;
    TRACE("DmDeviceCreateWithProvider: registered device %u, parent %u, kind %i",
          description->Id, description->ParentId, registration->Kind);
    usched_mtx_unlock(&g_devicesLock);

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
    struct DmDeviceRegistration registration = {
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
    status = DmDeviceCreateWithProvider(&registration, flags, idOut);
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
    struct __DmDevice* device;
    uuid_t*            ids;
    size_t             count = 0;
    size_t             index;
    TRACE("DmDeviceRefreshDrivers()");

    // Driver discovery may call back into the registry. Copy IDs under the
    // lock and let __TryLocateDriver check each entry again after unlocking.
    usched_mtx_lock(&g_devicesLock);
    ids = malloc(g_devices.count * sizeof(*ids));
    if (ids == NULL) {
        WARNING("DmDeviceRefreshDrivers: failed to allocate device ID snapshot");
        usched_mtx_unlock(&g_devicesLock);
        TRACE("DmDeviceRefreshDrivers: refreshing %zu devices", count);
        return;
    }
    foreach (i, &g_devices) {
        device = i->value;
        if (!device->Removing && !device->HasDriver && (device->Flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
            ids[count++] = device->Device->Id;
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
    struct __DmDevice*         device;
    struct __DmDeviceProtocol* protocol;
    DeviceIdentification_t*    identification;
    TRACE("DmDeviceDestroy(device=%u)", deviceId);

    usched_mtx_lock(&g_devicesLock);

    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL) {
        TRACE("DmDeviceDestroy: device %u was not found", deviceId);
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }

    // Stop new users before checking existing ones. On OS_EBUSY, the caller
    // must wait for requests, release DMA leases and remove children, then retry.
    device->Removing = 1;

    if (device->ActiveRequests != 0 || device->DmaLeases != 0) {
        WARNING("DmDeviceDestroy: device %u still has %u requests and %u DMA leases",
                deviceId, device->ActiveRequests, device->DmaLeases);
        usched_mtx_unlock(&g_devicesLock);
        return OS_EBUSY;
    }

    foreach (i, &g_devices) {
        if (((struct __DmDevice*)i->value)->Device->ParentId == deviceId) {
            WARNING("DmDeviceDestroy: device %u still has child %u",
                    deviceId, ((struct __DmDevice*)i->value)->Device->Id);
            usched_mtx_unlock(&g_devicesLock);
            return OS_EBUSY;
        }
    }

    list_remove(&g_devices, &device->Header);
    usched_mtx_unlock(&g_devicesLock);

    // Do not hold the registry lock while cancelling discovery work. A driver
    // completing startup must see either the live device or ENOENT.
    DmDiscoverForgetDevice(deviceId);

    while (device->Protocols.head != NULL) {
        protocol = device->Protocols.head->value;
        list_remove(&device->Protocols, &protocol->Header);
        free(protocol->Name);
        free(protocol);
    }

    if (device->Provider.Operations != NULL) {
        device->Provider.Operations->Release(device->Provider.Context);
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
