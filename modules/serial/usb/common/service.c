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

#include <ctt_driver_protocol_server.h>

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args*);

static gracht_protocol_function_t ctt_driver_callbacks[1] = {
        { PROTOCOL_CTT_DRIVER_REGISTER_DEVICE_ID , ctt_driver_register_device_callback },
};
DEFINE_CTT_DRIVER_SERVER_PROTOCOL(ctt_driver_callbacks, 1);

#include <ctt_usbhost_protocol_server.h>

extern void ctt_usbhost_queue_async_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_async_args*);
extern void ctt_usbhost_queue_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_args*);
extern void ctt_usbhost_queue_periodic_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_periodic_args*);
extern void ctt_usbhost_dequeue_callback(struct gracht_recv_message* message, struct ctt_usbhost_dequeue_args*);
extern void ctt_usbhost_query_port_callback(struct gracht_recv_message* message, struct ctt_usbhost_query_port_args*);
extern void ctt_usbhost_reset_port_callback(struct gracht_recv_message* message, struct ctt_usbhost_reset_port_args*);
extern void ctt_usbhost_reset_endpoint_callback(struct gracht_recv_message* message, struct ctt_usbhost_reset_endpoint_args*);

static gracht_protocol_function_t ctt_usbhost_callbacks[7] = {
    { PROTOCOL_CTT_USBHOST_QUEUE_ASYNC_ID , ctt_usbhost_queue_async_callback },
    { PROTOCOL_CTT_USBHOST_QUEUE_ID , ctt_usbhost_queue_callback },
    { PROTOCOL_CTT_USBHOST_QUEUE_PERIODIC_ID , ctt_usbhost_queue_periodic_callback },
    { PROTOCOL_CTT_USBHOST_DEQUEUE_ID , ctt_usbhost_dequeue_callback },
    { PROTOCOL_CTT_USBHOST_QUERY_PORT_ID , ctt_usbhost_query_port_callback },
    { PROTOCOL_CTT_USBHOST_RESET_PORT_ID , ctt_usbhost_reset_port_callback },
    { PROTOCOL_CTT_USBHOST_RESET_ENDPOINT_ID , ctt_usbhost_reset_endpoint_callback },
};
DEFINE_CTT_USBHOST_SERVER_PROTOCOL(ctt_usbhost_callbacks, 7);

extern void OnInterrupt(int, void*);

OsStatus_t
OnLoad(void)
{
    // Register supported protocols
    gracht_server_register_protocol(&ctt_driver_server_protocol);
    gracht_server_register_protocol(&ctt_usbhost_server_protocol);
    
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
