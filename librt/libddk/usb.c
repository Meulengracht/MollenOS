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
 */

#include <ddk/service.h>
#include <ddk/usbdevice.h>
#include <ddk/convert.h>
#include <gracht/link/vali.h>
#include <internal/_utils.h>
#include <signal.h>
#include <sys_usb_service_client.h>

extern int __crt_get_server_iod(void);

oserr_t
UsbControllerRegister(
        _In_ Device_t*           device,
        _In_ UsbControllerType_t type,
        _In_ int                 portCount)
{
    struct vali_link_message msg          = VALI_MSG_INIT_HANDLE(GetUsbService());
    uuid_t                   serverHandle = GetNativeHandle(__crt_get_server_iod());
    struct sys_device        protoDevice;

    to_sys_device(device, &protoDevice);
    sys_usb_register_controller(GetGrachtClient(), &msg.base, serverHandle,
                                &protoDevice, (int)type, (int)portCount);
    sys_device_destroy(&protoDevice);
    return OS_EOK;
}

void
UsbControllerUnregister(
        _In_ uuid_t deviceID)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());
    int                      status;

    status = sys_usb_unregister_controller(
            GetGrachtClient(),
            &msg.base,
            deviceID
    );
    if (status) {
        raise(SIGPIPE);
    }
}

oserr_t
UsbHubRegister(
        _In_ UsbDevice_t* usbDevice,
        _In_ int          portCount)
{
    struct vali_link_message msg          = VALI_MSG_INIT_HANDLE(GetUsbService());
    uuid_t                   serverHandle = GetNativeHandle(__crt_get_server_iod());

    sys_usb_register_hub(GetGrachtClient(), &msg.base,
                         usbDevice->DeviceContext.hub_device_id,
                         usbDevice->Base.Id,
                         serverHandle,
                         portCount);
    return OS_EOK;
}

oserr_t
UsbHubUnregister(
        _In_ uuid_t deviceId)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());

    sys_usb_unregister_hub(GetGrachtClient(), &msg.base, deviceId);
    return OS_EOK;
}

oserr_t
UsbEventPort(
        _In_ uuid_t  DeviceId,
        _In_ uint8_t PortAddress)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());
    
    status = sys_usb_port_event(
            GetGrachtClient(),
            &msg.base,
            DeviceId,
            PortAddress
    );
    return OS_EOK;
}

oserr_t
UsbPortError(
        _In_ uuid_t  deviceId,
        _In_ uint8_t portAddress)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());

    status = sys_usb_port_error(GetGrachtClient(), &msg.base, deviceId, portAddress);
    return OS_EOK;
}

oserr_t
UsbQueryControllerCount(
    _Out_ int* ControllerCount)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());
    
    status = sys_usb_get_controller_count(GetGrachtClient(), &msg.base);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_usb_get_controller_count_result(GetGrachtClient(), &msg.base, ControllerCount);
    return OS_EOK;
}

oserr_t
UsbQueryController(
        _In_ int                Index,
        _In_ USBControllerDevice_t* Controller)
{
    int                      status;
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetUsbService());
    
    status = sys_usb_get_controller(GetGrachtClient(), &msg.base, Index);
    gracht_client_await(GetGrachtClient(), &msg.base, GRACHT_AWAIT_ASYNC);
    sys_usb_get_controller_result(GetGrachtClient(), &msg.base, (uint8_t*)Controller, sizeof(struct USBControllerDevice));
    return OS_EOK;
}
