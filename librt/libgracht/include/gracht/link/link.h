/* MollenOS
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
 * Gracht Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_LINK_H__
#define __GRACHT_LINK_H__

#include "../types.h"

#define LINK_LISTEN_DGRAM  0
#define LINK_LISTEN_SOCKET 1

enum gracht_link_type {
    gracht_link_stream_based, // connection mode
    gracht_link_packet_based  // connection less mode
};

struct gracht_server_client {
    struct gracht_object_header header;
    uint32_t                    subscriptions[8]; // 32 bytes to cover 255 bits
    int                         iod;
};

struct server_link_ops;
struct client_link_ops;

typedef int (*server_create_client_fn)(struct server_link_ops*, struct gracht_recv_message*, struct gracht_server_client**);
typedef int (*server_recv_client_fn)(struct gracht_server_client*, struct gracht_recv_message*, unsigned int flags);
typedef int (*server_send_client_fn)(struct gracht_server_client*, struct gracht_message*, unsigned int flags);
typedef int (*server_destroy_client_fn)(struct gracht_server_client*);

typedef int  (*server_link_listen_fn)(struct server_link_ops*, int mode);
typedef int  (*server_link_accept_fn)(struct server_link_ops*, struct gracht_server_client**);
typedef int  (*server_link_recv_packet_fn)(struct server_link_ops*, struct gracht_recv_message*, unsigned int flags);
typedef int  (*server_link_respond_fn)(struct server_link_ops*, struct gracht_recv_message*, struct gracht_message*);
typedef void (*server_link_destroy_fn)(struct server_link_ops*);

struct server_link_ops {
    server_create_client_fn  create_client;
    server_destroy_client_fn destroy_client;
    server_recv_client_fn    recv_client;
    server_send_client_fn    send_client;

    server_link_listen_fn      listen;
    server_link_accept_fn      accept;
    server_link_recv_packet_fn recv_packet;
    server_link_respond_fn     respond;
    server_link_destroy_fn     destroy;
};

typedef int  (*client_link_connect_fn)(struct client_link_ops*);
typedef int  (*client_link_recv_fn)(struct client_link_ops*, void* messageBuffer, unsigned int flags, struct gracht_message**);
typedef int  (*client_link_send_fn)(struct client_link_ops*, struct gracht_message*, void* messageContext);
typedef void (*client_link_destroy_fn)(struct client_link_ops*);

struct client_link_ops {
    client_link_connect_fn connect;
    client_link_recv_fn    recv;
    client_link_send_fn    send;
    client_link_destroy_fn destroy;
};

#endif // !__GRACHT_LINK_H__
