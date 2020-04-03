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
 * Gracht Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __GRACHT_SERVER_H__
#define __GRACHT_SERVER_H__

#include "types.h"
#include <inet/socket.h>

typedef struct gracht_server_configuration {
    struct server_link_ops* link;
} gracht_server_configuration_t;

#ifdef __cplusplus
extern "C" {
#endif

// Server API
// This should be called for the compositor that wants to manage
// wm-clients. This will initiate data structures and setup handler threads
int gracht_server_initialize(gracht_server_configuration_t*);
int gracht_server_register_protocol(gracht_protocol_t*);
int gracht_server_unregister_protocol(gracht_protocol_t*);
int gracht_server_main_loop(void);
int gracht_server_respond(struct gracht_recv_message*, struct gracht_message*);
int gracht_server_send_event(int, struct gracht_message*, unsigned int);
int gracht_server_broadcast_event(struct gracht_message*, unsigned int);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_SERVER_H__
