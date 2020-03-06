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
 * Gracht Connection Type Definitions & Structures
 * - This header describes the base connection-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_CONNECTION_H__
#define __GRACHT_CONNECTION_H__

#include "types.h"

// Prototypes
struct sockaddr_storage;

// Connection API
// Used to manage all the connections to the window manager.
int gracht_connection_initialize(void);
int gracht_connection_create(int, struct sockaddr_storage*, int);
int gracht_connection_recv_packet(int, gracht_message_t*, void*, struct sockaddr_storage*, int);
int gracht_connection_recv_stream(int, gracht_message_t*, void*, int);
int gracht_connection_send_packet(int, gracht_message_t*, void*, size_t, struct sockaddr_storage*);
int gracht_connection_send_stream(int, gracht_message_t*, void*, size_t);
int gracht_connection_send_reply(int, void*, size_t, struct sockaddr_storage*);
int gracht_connection_broadcast_message(gracht_message_t*, void*, size_t);
int gracht_connection_shutdown(int);

#endif // !__GRACHT_CONNECTION_H__
