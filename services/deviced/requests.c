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

extern void DmHandleNotify(uuid_t driverId, uuid_t driverHandle);
extern void DmHandleGetDevicesByProtocol(struct gracht_message* message, uint8_t protocolID);
extern oserr_t DmHandleIoctl(uuid_t deviceID, unsigned int command, unsigned int flags);
extern oserr_t DmHandleIoctl2(uuid_t device_id, int direction, unsigned int command, size_t value, unsigned int width, size_t*);
extern void DmHandleRegisterProtocol(uuid_t deviceID, const char* protocolName, uint8_t protocolID);

void sys_device_notify_invocation(struct gracht_message* message,
                                  const uuid_t driverId, const uuid_t driverHandle)
{
    TRACE("sys_device_notify_invocation()");
    DmHandleNotify(driverId, driverHandle);
}

void sys_device_register_invocation(
        struct gracht_message* message, const struct sys_device* sysDevice, const unsigned int flags)
{
    Device_t* device;
    uuid_t    result = UUID_INVALID;
    oserr_t   status;
    TRACE("sys_device_register_invocation()");

    device = from_sys_device(sysDevice);
    if (device == NULL) {
        status = OS_EINVALPARAMS;
        goto respond;
    }

    status = DmDeviceCreate(device, flags, &result);

respond:
    sys_device_register_response(message, status, result);
}

void sys_device_unregister_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    TRACE("sys_device_unregister_invocation()");
    sys_device_unregister_response(message, OS_ENOTSUPPORTED);
}

void sys_device_ioctl_invocation(struct gracht_message* message,
                                 const uuid_t deviceId, const unsigned int command, const unsigned int flags)
{
    TRACE("sys_device_ioctl_invocation()");
    oserr_t oserr = DmHandleIoctl(deviceId, command, flags);
    sys_device_ioctl_response(message, oserr);
}

void sys_device_ioctlex_invocation(struct gracht_message* message, const uuid_t deviceId,
                                   const int direction, const unsigned int command,
                                   const size_t value, const unsigned int width)
{
    TRACE("sys_device_ioctlex_invocation()");
    size_t  result;
    oserr_t oserr = DmHandleIoctl2(deviceId, direction, command, value, width, &result);
    sys_device_ioctlex_response(message, oserr, result);
}

void sys_device_get_devices_by_protocol_invocation(struct gracht_message* message, const uint8_t protocolId)
{
    TRACE("sys_device_get_devices_by_protocol_invocation()");
    DmHandleGetDevicesByProtocol(message, protocolId);
}

void ctt_driver_event_device_protocol_invocation(gracht_client_t* client,
                                                 const uuid_t deviceId,
                                                 const char* protocolName,
                                                 const uint8_t protocolId)
{
    TRACE("ctt_driver_event_device_protocol_invocation()");
    DmHandleRegisterProtocol(deviceId, protocolName, protocolId);
}
