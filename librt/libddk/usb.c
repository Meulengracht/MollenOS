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
 * Usb Definitions & Structures
 * - This header describes the base usb-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <ddk/service.h>
#include <usb/usb.h>
#include <internal/_ipc.h>
#include <internal/_utils.h>
#include <gracht/server.h>

OsStatus_t
UsbControllerRegister(
    _In_ Device_t*      Device,
    _In_ UsbControllerType_t Type,
    _In_ size_t              Ports)
{
    int                      status;
    struct vali_link_message msg          = VALI_MSG_INIT_HANDLE(GetUsbService());
    UUId_t                   serverHandle = GetNativeHandle(gracht_server_get_dgram_iod());
    
    status = svc_usb_register(GetGrachtClient(), &msg, serverHandle, 
        Device, Device->Length, (int)Type, (int)Ports);
    gracht_vali_message_finish(&msg);
    return OsSuccess;
}

OsStatus_t
UsbControllerUnregister(
    _In_ UUId_t DeviceId)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());
    
    status = svc_usb_unregister(GetGrachtClient(), &msg, DeviceId);
    gracht_vali_message_finish(&msg);
    return OsSuccess;
}

OsStatus_t
UsbEventPort(
    _In_ UUId_t  DeviceId,
    _In_ uint8_t HubAddress,
    _In_ uint8_t PortAddress)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());
    
    status = svc_usb_port_event(GetGrachtClient(), &msg, DeviceId, HubAddress, PortAddress);
    gracht_vali_message_finish(&msg);
    return OsSuccess;
}

OsStatus_t
UsbQueryControllerCount(
    _Out_ int* ControllerCount)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());
    
    status = svc_usb_get_controller_count(GetGrachtClient(), &msg, ControllerCount);
    gracht_vali_message_finish(&msg);
    return OsSuccess;
}

OsStatus_t
UsbQueryController(
    _In_ int                Index,
    _In_ UsbHcController_t* Controller)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());
    
    status = svc_usb_get_controller(GetGrachtClient(), &msg, Index, Controller);
    gracht_vali_message_finish(&msg);
    return OsSuccess;
}
