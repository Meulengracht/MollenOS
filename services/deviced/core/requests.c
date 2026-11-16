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
 * Device Manager
 * - Implementation of the device manager in the operating system.
 *   Keeps track of devices, their loaded drivers and bus management.
 */

//#define __TRACE

#include <ddk/convert.h>
#include <core/dma.h>
#include <ddk/utils.h>
#include <gracht/client.h>
#include <devices.h>
#include <discover.h>

#include <sys_device_service_server.h>

extern void DmHandleGetDevicesByProtocol(struct gracht_message*, uint8_t);
extern oserr_t DmHandleIoctl(uint64_t, uuid_t, enum OSIOCtlRequest, void*, size_t);
extern oserr_t DmHandleIoctl2(uint64_t, uuid_t, int, unsigned int, size_t, unsigned int, size_t*);
extern void DmHandleRegisterProtocol(uuid_t, const char*, uint8_t);

void
sys_device_notify_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           driverId,
    _In_ const uuid_t           driverHandle)
{
    TRACE("sys_device_notify_invocation(driver=%u, handle=%u)", driverId, driverHandle);
    if (message->security.source != GRACHT_SECURITY_KERNEL) {
        WARNING("sys_device_notify_invocation: driver %u sent no authenticated credentials", driverId);
        return;
    }
    DmHandleNotify(message->security.principal, driverId, driverHandle);
}

void
sys_device_register_invocation(
    _In_ struct gracht_message*   message,
    _In_ const struct sys_device* sysDevice,
    _In_ const unsigned int       flags)
{
    Device_t* device;
    uuid_t    result = UUID_INVALID;
    oserr_t   status;
    int       responseStatus;
    TRACE("sys_device_register_invocation(type=%i, flags=0x%x)", sysDevice->content_type, flags);

    // Platform identities must come from the bus that owns the firmware resources.
    // Accepting them from a driver would allow it to use another controller's resources.
    if (sysDevice->content_type == SYS_DEVICE_CONTENT_PLATFORM) {
        WARNING("sys_device_register_invocation: rejecting driver-supplied platform identity");
        status = OS_ENOTSUPPORTED;
        goto respond;
    }

    device = from_sys_device(sysDevice);
    if (device == NULL) {
        WARNING("sys_device_register_invocation: failed to convert device description");
        status = OS_EINVALPARAMS;
        goto respond;
    }

    status = DmDeviceCreate(device, flags, &result);
    if (status != OS_EOK) {
        WARNING("sys_device_register_invocation: device registration failed: %u", status);
        // Registration takes ownership only on success.
        free(device->Identification.Description);
        free(device->Identification.Manufacturer);
        free(device->Identification.Product);
        free(device->Identification.Revision);
        free(device->Identification.Serial);
        free(device);
    }

respond:
    responseStatus = sys_device_register_response(message, status, result);
    if (responseStatus != 0) {
        WARNING("sys_device_register_invocation: response for device %u failed: %i",
                result, responseStatus);
    }
}

void
sys_device_unregister_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId)
{
    int status;
    TRACE("sys_device_unregister_invocation(device=%u): unsupported", deviceId);

    status = sys_device_unregister_response(message, OS_ENOTSUPPORTED);
    if (status != 0) {
        WARNING("sys_device_unregister_invocation: response for device %u failed: %i", deviceId, status);
    }
}

void
sys_device_ioctl_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId,
    _In_ const unsigned int     request,
    _In_ const uint8_t*         data,
    _In_ const uint32_t         data_count)
{
    uint64_t principal = 0;
    oserr_t  oserr;
    int      status;
    TRACE("sys_device_ioctl_invocation(device=%u, request=0x%x, length=%u)",
          deviceId, request, data_count);

    // The request body and reply endpoint say nothing reliable about who the
    // driver is. Only the kernel-supplied principal does. Keep all of its bits
    // until the registry has finished checking it.
    if (message->security.source == GRACHT_SECURITY_KERNEL) {
        principal = message->security.principal;
    }

    // Input requests return device data in the response buffer.
    if (request & __IOCTL_IN) {
        uint8_t out[data_count];
        memset(out, 0, data_count);

        oserr = DmHandleIoctl(
            principal,
            deviceId,
            (enum OSIOCtlRequest)request,
            &out[0],
            data_count
        );
        if (oserr != OS_EOK) {
            WARNING("sys_device_ioctl_invocation: device %u input request 0x%x failed: %u",
                    deviceId, request, oserr);
        }

        status = sys_device_ioctl_response(message, &out[0], data_count, oserr);
        if (status != 0) {
            WARNING("sys_device_ioctl_invocation: input response for device %u failed: %i",
                    deviceId, status);
        }
    } else {
        oserr = DmHandleIoctl(
            principal,
            deviceId,
            (enum OSIOCtlRequest)request,
            (void*)&data[0],
            data_count
        );
        if (oserr != OS_EOK) {
            WARNING("sys_device_ioctl_invocation: device %u output request 0x%x failed: %u",
                    deviceId, request, oserr);
        }

        status = sys_device_ioctl_response(message, NULL, 0, oserr);
        if (status != 0) {
            WARNING("sys_device_ioctl_invocation: output response for device %u failed: %i",
                    deviceId, status);
        }
    }
}

void
sys_device_ioctlex_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId,
    _In_ const int              direction,
    _In_ const unsigned int     command,
    _In_ const size_t           value,
    _In_ const unsigned int     width)
{
    uint64_t principal = 0;
    size_t   result = 0;
    oserr_t  oserr;
    int      status;
    TRACE("sys_device_ioctlex_invocation(device=%u, direction=%i, command=0x%x, width=%u)",
          deviceId, direction, command, width);

    if (message->security.source == GRACHT_SECURITY_KERNEL) {
        principal = message->security.principal;
    }

    oserr = DmHandleIoctl2(
        principal,
        deviceId,
        direction,
        command,
        value,
        width,
        &result
    );
    if (oserr != OS_EOK) {
        WARNING("sys_device_ioctlex_invocation: device %u command 0x%x failed: %u",
                deviceId, command, oserr);
    }

    status = sys_device_ioctlex_response(message, oserr, result);
    if (status != 0) {
        WARNING("sys_device_ioctlex_invocation: response for device %u failed: %i", deviceId, status);
    }
}

void
sys_device_get_devices_by_protocol_invocation(
    _In_ struct gracht_message* message,
    _In_ const uint8_t          protocolId)
{
    TRACE("sys_device_get_devices_by_protocol_invocation(protocol=%u)", protocolId);
    DmHandleGetDevicesByProtocol(message, protocolId);
}

void
ctt_driver_event_device_protocol_invocation(
    _In_ gracht_client_t* client,
    _In_ const uuid_t     deviceId,
    _In_ const char*      protocolName,
    _In_ const uint8_t    protocolId)
{
    TRACE("ctt_driver_event_device_protocol_invocation(device=%u, protocol=%u, name=%s)",
          deviceId, protocolId, protocolName);
    DmHandleRegisterProtocol(deviceId, protocolName, protocolId);
}

void
sys_device_dma_bind_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           deviceId,
    _In_ const uint64_t         requestId)
{
    OSDmaBindingResult_t binding = { 0 };
    oserr_t              status;
    int                  sent;

    if (message->security.source != GRACHT_SECURITY_KERNEL) {
        status = OS_EPERMISSIONS;
    } else {
        status = DmDmaBind(message->security.principal, deviceId, requestId, &binding);
    }

    if (status != OS_EOK) {
        WARNING("sys_device_dma_bind_invocation: device %u request %llu failed: %u",
                deviceId, (unsigned long long)requestId, status);
    }

    // The binding is saved before we send the reply. If the send fails, the
    // saved binding stays, so the driver can repeat the same request and get
    // the reply again.
    sent = sys_device_dma_bind_response(
        message,
        status,
        binding.Handle,
        binding.Generation
    );
    if (sent != 0) {
        WARNING("sys_device_dma_bind_invocation: device %u reply failed: %i", deviceId, sent);
    }
}
