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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * I/O Definitions & Structures
 * - This header describes the base io-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/device.h>
#include <internal/_ipc.h>

UUId_t
RegisterDevice(
    _In_ UUId_t         Parent,
    _In_ MCoreDevice_t* Device, 
    _In_ Flags_t        Flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    OsStatus_t               status;
    
    svc_device_register_sync(GetGrachtClient(), &msg, Parent, Device, Device->Length, Flags, &status);
    return status;
}

OsStatus_t
UnregisterDevice(
    _In_ UUId_t DeviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    OsStatus_t               status;
    
    svc_device_unregister_sync(GetGrachtClient(), &msg, DeviceId, &status);
    return status;
}

OsStatus_t
IoctlDevice(
    _In_ UUId_t  Device,
    _In_ Flags_t Command,
    _In_ Flags_t Flags)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    OsStatus_t               status;
    
    svc_device_ioctl_sync(GetGrachtClient(), &msg, Device, Command, Flags, &status);
    return status;
}

OsStatus_t
IoctlDeviceEx(
    _In_    UUId_t  Device,
    _In_    int     Direction,
    _In_    Flags_t Register,
    _InOut_ size_t* Value,
    _In_    size_t  Width)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetDeviceService());
    OsStatus_t               status;
    
    svc_device_ioctl_ex_sync(GetGrachtClient(), &msg, Device, Direction, Register,
        *Value, Width, &status, Value);
    return status;
}
