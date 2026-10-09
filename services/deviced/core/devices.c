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

#define __TRACE
//#define __OSCONFIG_NODRIVERS

#include <assert.h>
#include <core/dma.h>
#include <limits.h>
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
    element_t                    header;
    uuid_t                       driver_id;
    bool                         has_driver;
    unsigned int                 flags;
    Device_t*                    device;
    list_t                       Protocols;   // list<struct DmDeviceProtocol>
    enum DmDeviceDescriptionKind Kind;
    struct DmDeviceProvider      Provider;
    unsigned int                 ActiveRequests;
    unsigned int                 DmaLeases;
    int                          Removing;
};

// Describes a DMA lease associated with a device.
// These are not time-bound leases, they rather just inhibit
// destruction of the device while a lease is active.
struct DmDmaLease {
    struct DMDevice*        Device;
    struct DmDmaDescription Description;
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
    if (device == NULL || device->Removing || !(device->flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
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
        
        if (device->Removing) {
            continue;
        }
        
        foreach(protoNode, &device->Protocols) {
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

static oserr_t
__AcquireDeviceRequest(
    _In_  uuid_t            deviceId,
    _Out_ struct DMDevice** deviceOut)
{
    struct DMDevice* device;

    usched_mtx_lock(&g_devicesLock);
    
    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_ENOENT;
    }
    
    if (device->Removing) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_EBUSY;
    }
    
    // Wrapping the request count could allow removal during a live callback.
    if (device->ActiveRequests == UINT_MAX) {
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
    _In_ struct DMDevice* device)
{
    usched_mtx_lock(&g_devicesLock);
    device->ActiveRequests--;
    usched_mtx_unlock(&g_devicesLock);
}

oserr_t
DmDevicePrepareDma(
    _In_    uuid_t              deviceId,
    _InOut_ struct DmDmaLease** leaseOut)
{
    struct DMDevice*                         device;
    const struct DmDeviceProviderOperations* operations;
    struct DmDmaLease*                       lease;
    oserr_t                                  status;

    // Require an empty owning slot so preparing twice cannot lose an earlier
    // lease and permanently prevent the device from being removed.
    if (leaseOut == NULL) {
        return OS_EINVALPARAMS;
    }
    if (*leaseOut != NULL) {
        return OS_EBUSY;
    }

    status = __AcquireDeviceRequest(deviceId, &device);
    if (status != OS_EOK) {
        return status;
    }

    operations = device->Provider.Operations;
    if (operations == NULL || operations->PrepareDma == NULL) {
        __ReleaseDeviceRequest(device);
        return OS_ENOTSUPPORTED;
    }

    lease = calloc(1, sizeof(struct DmDmaLease));
    if (lease == NULL) {
        __ReleaseDeviceRequest(device);
        return OS_EOOM;
    }

    // The request guard keeps the entry and its registry-owned provider
    // reference alive. Bus callbacks may acquire their own locks or fail, so
    // run them without the registry lock and keep their result private.
    status = operations->PrepareDma(device->Provider.Context, &lease->Description);

    // Commit the lease before dropping request protection. Removal may have
    // started during the callback; in that case it wins and no lease escapes.
    usched_mtx_lock(&g_devicesLock);
    if (status == OS_EOK) {
        if (device->Removing) {
            status = OS_EBUSY;
        } else if (device->DmaLeases == UINT_MAX) {
            status = OS_EOVERFLOW;
        } else {
            device->DmaLeases++;
            lease->Device = device;
            lease->Description.DeviceId = deviceId;
        }
    }
    device->ActiveRequests--;
    usched_mtx_unlock(&g_devicesLock);

    if (status != OS_EOK) {
        free(lease);
        return status;
    }

    *leaseOut = lease;
    return OS_EOK;
}

const struct DmDmaDescription*
DmDmaLeaseGetDescription(
    _In_ const struct DmDmaLease* lease)
{
    // Borrow the copy rather than expose the registry or provider's context.
    return lease == NULL ? NULL : &lease->Description;
}

void
DmDmaLeaseRelease(
    _InOut_ struct DmDmaLease** lease)
{
    struct DmDmaLease* owned;

    // Clearing the owning slot makes repeated cleanup on that slot harmless.
    // No request lookup is needed: releasing must work after removal starts.
    if (lease == NULL || *lease == NULL) {
        return;
    }
    
    owned = *lease;
    *lease = NULL;

    usched_mtx_lock(&g_devicesLock);
    owned->Device->DmaLeases--;
    usched_mtx_unlock(&g_devicesLock);

    // The entry may now be destroyed by another thread. Touch only our copy.
    free(owned);
}

oserr_t
DmHandleIoctl(
    _In_ uuid_t              deviceId,
    _In_ enum OSIOCtlRequest request,
    _In_ void*               buffer,
    _In_ size_t              length)
{
    struct DMDevice*                         device;
    const struct DmDeviceProviderOperations* operations;
    uuid_t                                   driverId;
    oserr_t                                  status;

    status = __AcquireDeviceRequest(deviceId, &device);
    if (status != OS_EOK) {
        return status;
    }
    operations = device->Provider.Operations;

    // Driver selection may change independently of an active request.
    usched_mtx_lock(&g_devicesLock);
    driverId = device->driver_id;
    usched_mtx_unlock(&g_devicesLock);

    if (request == OSIOCTLREQUEST_BUS_CONTROL &&
        (buffer == NULL || length < sizeof(struct OSIOCtlBusControl))) {
        status = OS_EINVALPARAMS;
    } else if (request == OSIOCTLREQUEST_IO_REQUIREMENTS && driverId != UUID_INVALID) {
        status = OSDeviceIOCtl2(deviceId, driverId, request, buffer, length);
    } else if (operations != NULL && operations->Control != NULL) {
        status = operations->Control(device->Provider.Context, request, buffer, length);
    } else {
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
    struct DMDevice*                         device;
    const struct DmDeviceProviderOperations* operations;
    oserr_t                                  status;

    *valueOut = value;
    status = __AcquireDeviceRequest(deviceId, &device);
    if (status != OS_EOK) {
        return status;
    }
    
    operations = device->Provider.Operations;
    if (operations == NULL || operations->AccessRegister == NULL) {
        status = OS_ENOTSUPPORTED;
    } else {
        status = operations->AccessRegister(
            device->Provider.Context,
            direction,
            command,
            valueOut,
            width
        );
    }

    __ReleaseDeviceRequest(device);
    return status;
}

static oserr_t
__AddProtocolToDevice(
    _In_ const char*      protocolName,
    _In_ uint8_t          protocolID,
    _In_ struct DMDevice* device)
{
    struct DmDeviceProtocol* protocol;

    // Check if the protocol is already added to the device.
    foreach (node, &device->Protocols) {
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
    list_append(&device->Protocols, &protocol->header);
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
    if (device == NULL || device->Removing || device->driver_id == UUID_INVALID) {
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
    struct DMDevice*            device;
    struct DriverIdentification identification = { 0 };
    PlatformDevice_t*           platform;
    char                        compatibles[PLATFORM_DEVICE_MAX_COMPATIBLES];
    oserr_t                     status;

    usched_mtx_lock(&g_devicesLock);
    device = __GetDeviceUnsafe(deviceId);
    if (device == NULL || device->Removing || device->has_driver ||
        !(device->flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
        usched_mtx_unlock(&g_devicesLock);
        return;
    }
    
    identification.VendorId = device->device->VendorId;
    identification.ProductId = device->device->ProductId;
    identification.Class = device->device->Class;
    identification.Subclass = device->device->Subclass;
    
    if (device->Kind == DmDeviceDescriptionPlatform) {
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
    bindable = device != NULL && !device->Removing &&
        (device->flags & DEVICE_REGISTER_FLAG_LOADDRIVER);
    
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
    
    if (device->Removing) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_EBUSY;
    }

    device->flags |= DEVICE_REGISTER_FLAG_LOADDRIVER;
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
        if (operations->Retain == NULL || operations->Release == NULL) {
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
    _In_  unsigned int                       flags,
    _Out_ uuid_t*                            idOut)
{
    struct DMDevice* deviceNode;
    struct DMDevice* parent;
    Device_t*        description;
    oserr_t          status;

    *idOut = UUID_INVALID;
    
    if (!__ValidDeviceRegistration(registration)) {
        return OS_EINVALPARAMS;
    }

    deviceNode = calloc(1, sizeof(*deviceNode));
    if (deviceNode == NULL) {
        return OS_EOOM;
    }
    
    deviceNode->Provider = registration->Provider;
    if (deviceNode->Provider.Operations != NULL) {
        status = deviceNode->Provider.Operations->Retain(deviceNode->Provider.Context);
        if (status != OS_EOK) {
            free(deviceNode);
            return status;
        }
    }

    description = registration->Description;
    ELEMENT_INIT(&deviceNode->header, 0, deviceNode);
    deviceNode->driver_id = UUID_INVALID;
    deviceNode->device = description;
    deviceNode->Kind = registration->Kind;
    deviceNode->flags = flags;
    list_construct(&deviceNode->Protocols);

    usched_mtx_lock(&g_devicesLock);
    if (description->ParentId != UUID_INVALID) {
        parent = __GetDeviceUnsafe(description->ParentId);
        if (parent == NULL || parent->Removing) {
            usched_mtx_unlock(&g_devicesLock);
            if (deviceNode->Provider.Operations != NULL) {
                deviceNode->Provider.Operations->Release(deviceNode->Provider.Context);
            }
            free(deviceNode);
            return OS_ENOENT;
        }
    }

    description->Id = g_nextDeviceId++;
    deviceNode->header.key = (void*)(uintptr_t)description->Id;
    list_append(&g_devices, &deviceNode->header);

    *idOut = description->Id;
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

    // Older callers provide only the description. They get no provider merely
    // by supplying a bus address in that description.
    if (device != NULL) {
        if (device->Length == sizeof(BusDevice_t)) {
            registration.Kind = DmDeviceDescriptionBus;
        } else if (device->Length == sizeof(PlatformDevice_t)) {
            registration.Kind = DmDeviceDescriptionPlatform;
        } else if (device->Length == sizeof(UsbDevice_t)) {
            registration.Kind = DmDeviceDescriptionUsb;
        }
    }
    return DmDeviceCreateWithProvider(&registration, flags, idOut);
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
        if (!device->Removing && !device->has_driver && (device->flags & DEVICE_REGISTER_FLAG_LOADDRIVER)) {
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
    
    device->Removing = 1;
    
    if (device->ActiveRequests != 0 || device->DmaLeases != 0) {
        usched_mtx_unlock(&g_devicesLock);
        return OS_EBUSY;
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
    
    while (device->Protocols.head != NULL) {
        protocol = device->Protocols.head->value;
        list_remove(&device->Protocols, &protocol->header);
        free(protocol->name);
        free(protocol);
    }
    
    if (device->Provider.Operations != NULL) {
        device->Provider.Operations->Release(device->Provider.Context);
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
