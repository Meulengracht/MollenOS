/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Network Manager
 * - Contains the implementation of the network-manager which keeps track
 *   of sockets, network interfaces and connectivity status
 */

//#define __TRACE

#include <ddk/service.h>
#include <ddk/utils.h>
#include "manager.h"

#include <svc_socket_protocol_server.h>

extern void svc_socket_create_callback(struct gracht_recv_message* message, struct svc_socket_create_args*);
extern void svc_socket_close_callback(struct gracht_recv_message* message, struct svc_socket_close_args*);
extern void svc_socket_bind_callback(struct gracht_recv_message* message, struct svc_socket_bind_args*);
extern void svc_socket_connect_callback(struct gracht_recv_message* message, struct svc_socket_connect_args*);
extern void svc_socket_accept_callback(struct gracht_recv_message* message, struct svc_socket_accept_args*);
extern void svc_socket_listen_callback(struct gracht_recv_message* message, struct svc_socket_listen_args*);
extern void svc_socket_pair_callback(struct gracht_recv_message* message, struct svc_socket_pair_args*);
extern void svc_socket_set_option_callback(struct gracht_recv_message* message, struct svc_socket_set_option_args*);
extern void svc_socket_get_option_callback(struct gracht_recv_message* message, struct svc_socket_get_option_args*);
extern void svc_socket_get_address_callback(struct gracht_recv_message* message, struct svc_socket_get_address_args*);

static gracht_protocol_function_t svc_socket_callbacks[10] = {
    { PROTOCOL_SVC_SOCKET_CREATE_ID , svc_socket_create_callback },
    { PROTOCOL_SVC_SOCKET_CLOSE_ID , svc_socket_close_callback },
    { PROTOCOL_SVC_SOCKET_BIND_ID , svc_socket_bind_callback },
    { PROTOCOL_SVC_SOCKET_CONNECT_ID , svc_socket_connect_callback },
    { PROTOCOL_SVC_SOCKET_ACCEPT_ID , svc_socket_accept_callback },
    { PROTOCOL_SVC_SOCKET_LISTEN_ID , svc_socket_listen_callback },
    { PROTOCOL_SVC_SOCKET_PAIR_ID , svc_socket_pair_callback },
    { PROTOCOL_SVC_SOCKET_SET_OPTION_ID , svc_socket_set_option_callback },
    { PROTOCOL_SVC_SOCKET_GET_OPTION_ID , svc_socket_get_option_callback },
    { PROTOCOL_SVC_SOCKET_GET_ADDRESS_ID , svc_socket_get_address_callback },
};
DEFINE_SVC_SOCKET_SERVER_PROTOCOL(svc_socket_callbacks, 10);

OsStatus_t OnUnload(void)
{
    return OsSuccess;
}

void GetServiceAddress(struct ipmsg_addr* address)
{
    address->type = IPMSG_ADDRESS_PATH;
    address->data.path = SERVICE_NET_PATH;
}

OsStatus_t
OnLoad(void)
{
    // Register supported interfaces
    gracht_server_register_protocol(&svc_socket_server_protocol);
    return NetworkManagerInitialize();
}
