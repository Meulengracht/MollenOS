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
 * Usb Manager
 * - Contains the implementation of the usb-manager which keeps track
 *   of all usb-controllers and their devices
 */

#include <usb/usb.h>
#include <ddk/utils.h>
#include <internal/_ipc.h>
#include "manager.h"

#include <svc_usb_protocol_server.h>

extern void svc_usb_register_callback(struct gracht_recv_message* message, struct svc_usb_register_args*);
extern void svc_usb_unregister_callback(struct gracht_recv_message* message, struct svc_usb_unregister_args*);
extern void svc_usb_port_event_callback(struct gracht_recv_message* message, struct svc_usb_port_event_args*);
extern void svc_usb_get_controller_count_callback(struct gracht_recv_message* message);
extern void svc_usb_get_controller_callback(struct gracht_recv_message* message, struct svc_usb_get_controller_args*);

static gracht_protocol_function_t svc_usb_callbacks[5] = {
    { PROTOCOL_SVC_USB_REGISTER_ID , svc_usb_register_callback },
    { PROTOCOL_SVC_USB_UNREGISTER_ID , svc_usb_unregister_callback },
    { PROTOCOL_SVC_USB_PORT_EVENT_ID , svc_usb_port_event_callback },
    { PROTOCOL_SVC_USB_GET_CONTROLLER_COUNT_ID , svc_usb_get_controller_count_callback },
    { PROTOCOL_SVC_USB_GET_CONTROLLER_ID , svc_usb_get_controller_callback },
};
DEFINE_SVC_USB_SERVER_PROTOCOL(svc_usb_callbacks, 5);

OsStatus_t OnUnload(void)
{
    return UsbCoreDestroy();
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_USB_PATH;
}

OsStatus_t
OnLoad(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(&svc_usb_server_protocol);
    return UsbCoreInitialize();
}
