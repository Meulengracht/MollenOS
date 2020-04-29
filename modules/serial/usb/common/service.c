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
 * USB Controller Manager
 * - Contains the implementation of a shared controller manager
 *   for all the usb drivers
 */
//#define __TRACE

#include <ddk/utils.h>
#include "hci.h"
#include <os/mollenos.h>
#include <stdlib.h>
#include <signal.h>

#include "ctt_driver_protocol_server.h"
#include "ctt_usbhost_protocol_server.h"

extern void OnInterrupt(int, void*);

OsStatus_t
OnLoad(void)
{
    sigprocess(SIGINT, OnInterrupt);
    
    // Register supported protocols
    gracht_server_register_protocol(&ctt_driver_protocol);
    gracht_server_register_protocol(&ctt_usbhost_protocol);
    
    return UsbManagerInitialize();
}

OsStatus_t
OnUnload(void)
{
    signal(SIGINT, SIG_DFL);
    return UsbManagerDestroy();
}

OsStatus_t
OnRegister(
    _In_ Device_t* Device)
{
    if (Device->Length != sizeof(BusDevice_t)) {
        return OsInvalidParameters;
    }
    
    if (HciControllerCreate((BusDevice_t*)Device) == NULL) {
        return OsError;
    }
    else {
        return OsSuccess;
    }
}

void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args* args)
{
    OnRegister(args->device);
}

OsStatus_t
OnUnregister(
    _In_ Device_t *Device)
{
    UsbManagerController_t* Controller = UsbManagerGetController(Device->Id);
    if (Controller == NULL) {
        return OsError;
    }
    return HciControllerDestroy(Controller);
}

void ctt_usbhost_query_port_callback(struct gracht_recv_message* message, struct ctt_usbhost_query_port_args* args)
{
    UsbHcPortDescriptor_t   descriptor;
    UsbManagerController_t* controller = UsbManagerGetController(args->device_id);
    HciPortGetStatus(controller, (int)args->port_id, &descriptor);
    ctt_usbhost_query_port_response(message, OsSuccess, &descriptor);
}

void ctt_usbhost_reset_port_callback(struct gracht_recv_message* message, struct ctt_usbhost_reset_port_args* args)
{
    UsbHcPortDescriptor_t   descriptor;
    UsbManagerController_t* controller = UsbManagerGetController(args->device_id);
    OsStatus_t              status     = HciPortReset(controller, (int)args->port_id);
    
    HciPortGetStatus(controller, (int)args->port_id, &descriptor);
    ctt_usbhost_reset_port_response(message, status, &descriptor);
}
