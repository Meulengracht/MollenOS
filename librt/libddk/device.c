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
#include <ddk/convert.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>

uuid_t
RegisterDevice(
    _In_ Device_t*    device,
    _In_ unsigned int flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    int                      status;
    oserr_t                  oserr;
    uuid_t                   id;
    struct sys_device        sysDevice;

    to_sys_device(device, &sysDevice);

    status = sys_device_register(GetGrachtClient(), &msg.base, &sysDevice,  flags);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_device_register_result(GetGrachtClient(), &msg.base, &oserr, &id);

    sys_device_destroy(&sysDevice);
    if (status) {
        ERROR("[ddk] [device] failed to register new device, errno %i", errno);
        return UUID_INVALID;
    }
    
    if (oserr != OS_EOK) {
        return UUID_INVALID;
    }
    
    device->Id = id;
    return id;
}

oserr_t
UnregisterDevice(
        _In_ uuid_t deviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    oserr_t                  oserr;
    
    sys_device_unregister(GetGrachtClient(), &msg.base, deviceId);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_device_unregister_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
IoctlDevice(
        _In_ uuid_t       deviceId,
        _In_ unsigned int command,
        _In_ unsigned int flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    oserr_t                  oserr;
    
    sys_device_ioctl(GetGrachtClient(), &msg.base, deviceId, command, flags);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_device_ioctl_result(GetGrachtClient(), &msg.base, &oserr);
    return oserr;
}

oserr_t
IoctlDeviceEx(
        _In_    uuid_t       deviceId,
        _In_    int          direction,
        _In_    unsigned int offset,
        _InOut_ size_t*      value,
        _In_    size_t       width)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    oserr_t                  oserr;
    
    sys_device_ioctlex(GetGrachtClient(), &msg.base, deviceId, direction, offset, *value, width);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_device_ioctlex_result(GetGrachtClient(), &msg.base, &oserr, value);
    return oserr;
}
