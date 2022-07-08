/**
 * MollenOS
 *
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
 *
 *
 * I/O Definitions & Structures
 * - This header describes the base io-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/device.h>
#include <ddk/utils.h>
#include <errno.h>
#include <internal/_ipc.h>

uuid_t
RegisterDevice(
    _In_ Device_t* device,
    _In_ unsigned int   flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    int                      status;
    oserr_t               osStatus;
    uuid_t                   id;

    status = sys_device_register(GetGrachtClient(), &msg.base, (uint8_t*)device, device->Length, flags);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_device_register_result(GetGrachtClient(), &msg.base, &osStatus, &id);
    if (status) {
        ERROR("[ddk] [device] failed to register new device, errno %i", errno);
        return UUID_INVALID;
    }
    
    if (osStatus != OsOK) {
        return UUID_INVALID;
    }
    
    device->Id = id;
    return id;
}

oserr_t
UnregisterDevice(
        _In_ uuid_t DeviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    oserr_t               status;
    
    sys_device_unregister(GetGrachtClient(), &msg.base, DeviceId);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_device_unregister_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

oserr_t
IoctlDevice(
        _In_ uuid_t  Device,
        _In_ unsigned int Command,
        _In_ unsigned int Flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    oserr_t               status;
    
    sys_device_ioctl(GetGrachtClient(), &msg.base, Device, Command, Flags);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_device_ioctl_result(GetGrachtClient(), &msg.base, &status);
    return status;
}

oserr_t
IoctlDeviceEx(
        _In_    uuid_t  Device,
        _In_    int     Direction,
        _In_    unsigned int Register,
        _InOut_ size_t* Value,
        _In_    size_t  Width)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    oserr_t               status;
    
    sys_device_ioctlex(GetGrachtClient(), &msg.base, Device, Direction, Register, *Value, Width);
    gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    sys_device_ioctlex_result(GetGrachtClient(), &msg.base, &status, Value);
    return status;
}
