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

#include <ioset.h>
#include "hci.h"

#include <ctt_driver_protocol_server.h>

static void ctt_driver_register_device_callback(struct gracht_recv_message* message, struct ctt_driver_register_device_args*);

static gracht_protocol_function_t ctt_driver_callbacks[1] = {
        { PROTOCOL_CTT_DRIVER_REGISTER_DEVICE_ID , ctt_driver_register_device_callback },
};
DEFINE_CTT_DRIVER_SERVER_PROTOCOL(ctt_driver_callbacks, 1);

#include <ctt_usbhost_protocol_server.h>
#include <ctt_usbhub_protocol_server.h>
#include <io.h>
#include <ddk/utils.h>

extern void ctt_usbhost_queue_async_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_async_args*);
extern void ctt_usbhost_queue_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_args*);
extern void ctt_usbhost_queue_periodic_callback(struct gracht_recv_message* message, struct ctt_usbhost_queue_periodic_args*);
extern void ctt_usbhost_dequeue_callback(struct gracht_recv_message* message, struct ctt_usbhost_dequeue_args*);
extern void ctt_usbhost_reset_endpoint_callback(struct gracht_recv_message* message, struct ctt_usbhost_reset_endpoint_args*);

static gracht_protocol_function_t ctt_usbhost_callbacks[5] = {
    { PROTOCOL_CTT_USBHOST_QUEUE_ASYNC_ID , ctt_usbhost_queue_async_callback },
    { PROTOCOL_CTT_USBHOST_QUEUE_ID , ctt_usbhost_queue_callback },
    { PROTOCOL_CTT_USBHOST_QUEUE_PERIODIC_ID , ctt_usbhost_queue_periodic_callback },
    { PROTOCOL_CTT_USBHOST_DEQUEUE_ID , ctt_usbhost_dequeue_callback },
    { PROTOCOL_CTT_USBHOST_RESET_ENDPOINT_ID , ctt_usbhost_reset_endpoint_callback },
};
DEFINE_CTT_USBHOST_SERVER_PROTOCOL(ctt_usbhost_callbacks, 5);

static void ctt_usbhub_query_port_callback(struct gracht_recv_message* message, struct ctt_usbhub_query_port_args*);
static void ctt_usbhub_reset_port_callback(struct gracht_recv_message* message, struct ctt_usbhub_reset_port_args*);

static gracht_protocol_function_t ctt_usbhub_callbacks[2] = {
    { PROTOCOL_CTT_USBHUB_QUERY_PORT_ID , ctt_usbhub_query_port_callback },
    { PROTOCOL_CTT_USBHUB_RESET_PORT_ID , ctt_usbhub_reset_port_callback },
};
DEFINE_CTT_USBHUB_SERVER_PROTOCOL(ctt_usbhub_callbacks, 2);

OsStatus_t
OnLoad(void)
{
    // Register supported protocols
    gracht_server_register_protocol(&ctt_driver_server_protocol);
    gracht_server_register_protocol(&ctt_usbhost_server_protocol);
    gracht_server_register_protocol(&ctt_usbhub_server_protocol);
    
    return UsbManagerInitialize();
}

OsStatus_t
OnUnload(void)
{
    UsbManagerDestroy();
    return OsSuccess;
}

OsStatus_t OnEvent(struct ioset_event* event)
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
        return OsSuccess;
    }

    return OsDoesNotExist;
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
    return OsSuccess;
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

static void ctt_usbhub_query_port_callback(
        _In_ struct gracht_recv_message*        message,
        _In_ struct ctt_usbhub_query_port_args* args)
{
    UsbHcPortDescriptor_t   descriptor;
    UsbManagerController_t* controller = UsbManagerGetController(args->device_id);
    if (!controller) {
        ctt_usbhub_query_port_response(message, OsInvalidParameters, &descriptor);
        return;
    }

    HciPortGetStatus(controller, (int)args->port_id, &descriptor);
    ctt_usbhub_query_port_response(message, OsSuccess, &descriptor);
}

static void ctt_usbhub_reset_port_callback(
        _In_ struct gracht_recv_message*        message,
        _In_ struct ctt_usbhub_reset_port_args* args)
{
    UsbHcPortDescriptor_t   descriptor;
    OsStatus_t              status;
    UsbManagerController_t* controller = UsbManagerGetController(args->device_id);
    if (!controller) {
        ctt_usbhub_reset_port_response(message, OsInvalidParameters, &descriptor);
        return;
    }

    status = HciPortReset(controller, (int)args->port_id);
    HciPortGetStatus(controller, (int)args->port_id, &descriptor);
    ctt_usbhub_reset_port_response(message, status, &descriptor);
}
