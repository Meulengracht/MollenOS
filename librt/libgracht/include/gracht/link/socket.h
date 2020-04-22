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
 * Gracht Socket Link Type Definitions & Structures
 * - This header describes the base link-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_LINK_SOCKET_CLIENT_H__
#define __GRACHT_LINK_SOCKET_CLIENT_H__

#if defined(MOLLENOS)
#include <inet/socket.h>
#include <io.h>
#elif defined(__linux__)
#include <unistd.h>
#include <sys/socket.h>
#else
#error "Undefined platform for socket"
#endif

#include "link.h"
#include "../client.h"

struct socket_server_configuration {
    struct sockaddr_storage server_address;
    socklen_t               server_address_length;
    
    struct sockaddr_storage dgram_address;
    socklen_t               dgram_address_length;
};

struct socket_client_configuration {
    enum gracht_link_type   type;
    struct sockaddr_storage address;
    socklen_t               address_length;
};

#ifdef __cplusplus
extern "C" {
#endif

// Link API
int gracht_link_socket_server_create(struct server_link_ops** linkOut, 
    struct socket_server_configuration* configuration);
int gracht_link_socket_client_create(struct client_link_ops** linkOut, 
    struct socket_client_configuration* configuration);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_LINK_SOCKET_CLIENT_H__
