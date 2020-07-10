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
#include "link/link.h"

typedef struct gracht_client_configuration {
    struct client_link_ops* link;
} gracht_client_configuration_t;

typedef struct gracht_client gracht_client_t;

#ifdef __cplusplus
extern "C" {
#endif

// Client API
// An application can utilize multiple clients, that connect to different
// servers. When invoking a protocol the specific client can be specified.
int gracht_client_create(gracht_client_configuration_t*, gracht_client_t**);
int gracht_client_register_protocol(gracht_client_t*, gracht_protocol_t*);
int gracht_client_unregister_protocol(gracht_client_t*, gracht_protocol_t*);
int gracht_client_shutdown(gracht_client_t*);
int gracht_client_iod(gracht_client_t*);

int gracht_client_wait_message(gracht_client_t *client, struct gracht_message_context *context, void *messageBuffer,
                               unsigned int flags);
int gracht_client_invoke(gracht_client_t*, struct gracht_message_context*, struct gracht_message*);
int gracht_client_await(gracht_client_t*, struct gracht_message_context*);
int gracht_client_await_multiple(gracht_client_t*, struct gracht_message_context**, int, unsigned int);
int gracht_client_status(gracht_client_t*, struct gracht_message_context*, struct gracht_param*);

#ifdef __cplusplus
}
#endif
#endif // !__GRACHT_CLIENT_H__
