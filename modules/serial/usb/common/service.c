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
 *
 * USB Controller Manager
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */
//#define __TRACE

#include <ioset.h>
#include "hci.h"

#include <ctt_driver_service_server.h>
#include <ctt_usbhost_service_server.h>
#include <ctt_usbhub_service_server.h>
#include <io.h>
#include <ddk/utils.h>

extern gracht_server_t* __crt_get_module_server(void);

oserr_t
OnLoad(void)
{
    // Register supported protocols
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_driver_server_protocol);
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_usbhost_server_protocol);
    gracht_server_register_protocol(__crt_get_module_server(), &ctt_usbhub_server_protocol);
    
    return UsbManagerInitialize();
}

oserr_t
OnUnload(void)
{
    UsbManagerDestroy();
    return OsOK;
}

oserr_t OnEvent(struct ioset_event* event)
{
    if (event->events & IOSETSYN) {
        UsbManagerController_t* controller = event->data.context;
        unsigned int            value;

        int bytesRead = read(controller->event_descriptor, &value, sizeof(unsigned int));
        if (bytesRead != sizeof(unsigned int)) {
            ERROR("[OnEvent] read failed %i :(", bytesRead);
            return OsError;
        }

        HciInterruptCallback(controller);
        return OsOK;
    }

    return OsNotExists;
}

oserr_t
OnRegister(
    _In_ Device_t* Device)
{
    if (Device->Length != sizeof(BusDevice_t)) {
        return OsInvalidParameters;
    }
    
    if (HciControllerCreate((BusDevice_t*)Device) == NULL) {
        return OsError;
    }
    return OsOK;
}

void ctt_driver_register_device_invocation(struct gracht_message* message,
        const uint8_t* device, const uint32_t device_count)
{
    OnRegister((Device_t*)device);
}

oserr_t
OnUnregister(
    _In_ Device_t *Device)
{
    UsbManagerController_t* Controller = UsbManagerGetController(Device->Id);
    if (Controller == NULL) {
        return OsError;
    }
    return HciControllerDestroy(Controller);
}

void ctt_usbhub_query_port_invocation(struct gracht_message* message, const uuid_t deviceId, const uint8_t portId)
{
    UsbHcPortDescriptor_t   descriptor;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    if (!controller) {
        ctt_usbhub_query_port_response(message, OsInvalidParameters, (uint8_t*)&descriptor, sizeof(UsbHcPortDescriptor_t));
        return;
    }

    HciPortGetStatus(controller, (int)portId, &descriptor);
    ctt_usbhub_query_port_response(message, OsOK, (uint8_t*)&descriptor, sizeof(UsbHcPortDescriptor_t));
}

void ctt_usbhub_reset_port_invocation(struct gracht_message* message, const uuid_t deviceId, const uint8_t portId)
{
    UsbHcPortDescriptor_t   descriptor;
    oserr_t              status;
    UsbManagerController_t* controller = UsbManagerGetController(deviceId);
    if (!controller) {
        ctt_usbhub_reset_port_response(message, OsInvalidParameters, (uint8_t*)&descriptor, sizeof(UsbHcPortDescriptor_t));
        return;
    }

    status = HciPortReset(controller, (int)portId);
    HciPortGetStatus(controller, (int)portId, &descriptor);
    ctt_usbhub_reset_port_response(message, status, (uint8_t*)&descriptor, sizeof(UsbHcPortDescriptor_t));
}

void ctt_driver_get_device_protocols_invocation(struct gracht_message* message, const uuid_t deviceId)
{
    //
}

// again lazyness in libddk causing this
void sys_device_event_protocol_device_invocation(void) {

}
void sys_device_event_device_update_invocation(void) {

}
