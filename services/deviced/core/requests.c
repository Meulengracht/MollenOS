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
#include <ddk/utils.h>
#include <gracht/client.h>
#include <devices.h>

#include <sys_device_service_server.h>

extern void DmHandleNotify(uuid_t, uuid_t);
extern void DmHandleGetDevicesByProtocol(struct gracht_message*, uint8_t);
extern oserr_t DmHandleIoctl(uuid_t, uuid_t, enum OSIOCtlRequest, void*, size_t);
extern oserr_t DmHandleIoctl2(uuid_t, int, unsigned int, size_t, unsigned int, size_t*);
extern void DmHandleRegisterProtocol(uuid_t, const char*, uint8_t);

void
sys_device_notify_invocation(
    _In_ struct gracht_message* message,
    _In_ const uuid_t           driverId,
    _In_ const uuid_t           driverHandle)
{
    TRACE("sys_device_notify_invocation(driver=%u, handle=%u)", driverId, driverHandle);
    DmHandleNotify(driverId, driverHandle);
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
    uuid_t  principal = UUID_INVALID;
    oserr_t oserr;
    int     status;
    TRACE("sys_device_ioctl_invocation(device=%u, request=0x%x, length=%u)",
          deviceId, request, data_count);

    // Only the transport's kernel-supplied sender identity can establish who
    // owns an interrupt group. The reply endpoint and request body cannot.
    if (message->security.source == GRACHT_SECURITY_KERNEL &&
        message->security.principal <= UINT32_MAX) {
        principal = (uuid_t)message->security.principal;
    }

    TRACE("sys_device_ioctl_invocation: resolved principal %u for device %u", principal, deviceId);
    if (request == OSIOCTLREQUEST_INTERRUPT_CONTROL && principal == UUID_INVALID) {
        WARNING("sys_device_ioctl_invocation: interrupt control for device %u has no valid kernel-supplied principal",
                deviceId);
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
    size_t  result;
    oserr_t oserr;
    int     status;
    TRACE("sys_device_ioctlex_invocation(device=%u, direction=%i, command=0x%x, width=%u)",
          deviceId, direction, command, width);

    oserr = DmHandleIoctl2(deviceId, direction, command, value, width, &result);
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
