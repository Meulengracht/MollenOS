/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <ddk/service.h>
#include <os/device.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>

#include <ctt_driver_service_client.h>
#include <sys_device_service_client.h>

oserr_t
OSDeviceIOCtl(
        _In_ uuid_t              deviceID,
        _In_ enum OSIOCtlRequest request,
        _In_ void*               buffer,
        _In_ size_t              length)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    oserr_t                  oserr;
    uint8_t*                 u8 = buffer;
    int                      status;

    status = sys_device_ioctl(
            GetGrachtClient(),
            &msg.base,
            deviceID,
            (unsigned int)request,
            u8,
            length
    );
    if (status) {
        return OsErrToErrNo(status);
    }

    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_device_ioctl_result(
            GetGrachtClient(),
            &msg.base,
            u8,
            length,
            &oserr
    );
    return oserr;
}

oserr_t
OSDeviceIOCtl2(
        _In_ uuid_t              deviceID,
        _In_ uuid_t              driverID,
        _In_ enum OSIOCtlRequest request,
        _In_ void*               buffer,
        _In_ size_t              length)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(driverID);
    oserr_t                  oserr;
    uint8_t*                 u8 = buffer;
    int                      status;

    status = ctt_driver_ioctl(
            GetGrachtClient(),
            &msg.base,
            deviceID,
            (unsigned int)request,
            u8,
            length
    );
    if (status) {
        return OsErrToErrNo(status);
    }

    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    ctt_driver_ioctl_result(
            GetGrachtClient(),
            &msg.base,
            u8,
            length,
            &oserr
    );
    return oserr;
}

// need to be defined unfortunately
void ctt_driver_event_device_protocol_invocation(gracht_client_t* client, const uuid_t deviceId, const char* protocolName, const uint8_t protocolId) { }
void sys_device_event_protocol_device_invocation(gracht_client_t* client, const uuid_t deviceId, const uuid_t driverId, const uint8_t protocolId) { }
void sys_device_event_device_update_invocation(gracht_client_t* client, const uuid_t deviceId, const uint8_t connected) { }
