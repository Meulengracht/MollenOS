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
 * Gracht Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_CLIENT_H__
#define __GRACHT_CLIENT_H__

#include "types.h"
#include <inet/socket.h>

enum gracht_client_type {
    gracht_client_stream_based, // connection mode
    gracht_client_packet_based  // connection less mode
};

typedef struct gracht_client_configuration {
    enum gracht_client_type type;
    struct sockaddr_storage address;
    socklen_t               address_length;
} gracht_client_configuration_t;

typedef struct gracht_client gracht_client_t;

// Client API
// An application can utilize multiple clients, that connect to different
// servers. When invoking a protocol the specific client can be specified.
int gracht_client_create(gracht_client_configuration_t*, gracht_client_t**);
int gracht_client_wait_message(gracht_client_t*, void*);
int gracht_client_process_message(gracht_client_t*, void*);
int gracht_client_register_protocol(gracht_client_t*, gracht_protocol_t*);
int gracht_client_unregister_protocol(gracht_client_t*, gracht_protocol_t*);
int gracht_client_invoke(gracht_client_t*, uint8_t, uint8_t, void*, size_t, void*, size_t);
int gracht_client_shutdown(gracht_client_t*);

#endif // !__GRACHT_CLIENT_H__
