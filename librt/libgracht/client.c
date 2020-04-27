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

#include <assert.h>
#include <errno.h>
#include "include/gracht/client.h"
#include "include/gracht/crc.h"
#include "include/gracht/list.h"
#include "include/gracht/debug.h"
#include <signal.h>
#include <string.h>
#include <stdlib.h>

typedef struct gracht_client {
    uint32_t                client_id;
    int                     iod;
    struct client_link_ops* ops;
    struct gracht_list      protocols;
} gracht_client_t;

extern int client_invoke_action(struct gracht_list*, struct gracht_recv_message*);

int gracht_client_invoke(gracht_client_t* client, struct gracht_message* message, void* context)
{
    if (!client || !message) {
        errno = (EINVAL);
        return -1;
    }
    
    if (message->header.length > GRACHT_MAX_MESSAGE_SIZE) {
        errno = (EINVAL);
        return -1;
    }
    
    return client->ops->send(client->ops, message, context);
}

int gracht_client_process_message(gracht_client_t* client, struct gracht_recv_message* message)
{
    if (!client || !message) {
        errno = (EINVAL);
        return -1;
    }
    return client_invoke_action(&client->protocols, message);
}

int gracht_client_wait_message(gracht_client_t* client, struct gracht_recv_message* message)
{
    if (!client) {
        errno = (EINVAL);
        return -1;
    }
    return client->ops->recv(client->ops, message, 0);
}

int gracht_client_create(gracht_client_configuration_t* config, gracht_client_t** client_out)
{
    gracht_client_t* client;
    
    client = (gracht_client_t*)malloc(sizeof(gracht_client_t));
    if (!client) {
        ERROR("gracht_client: failed to allocate memory for client data\n");
        errno = (ENOMEM);
        return -1;
    }
    
    if (!config || !config->link) {
        ERROR("[gracht] [client] config or config link was null");
        errno = EINVAL;
        return -1;
    }
    
    memset(client, 0, sizeof(gracht_client_t));
    client->ops = config->link;
    client->iod = client->ops->connect(client->ops);
    if (client->iod < 0) {
        ERROR("gracht_client: failed to connect client\n");
        free(client);
        return -1;
    }
    
    *client_out = client;
    return 0;
}

int gracht_client_register_protocol(gracht_client_t* client, gracht_protocol_t* protocol)
{
    if (!client || !protocol) {
        errno = (EINVAL);
        return -1;
    }
    
    gracht_list_append(&client->protocols, &protocol->header);
    return 0;
}

int gracht_client_unregister_protocol(gracht_client_t* client, gracht_protocol_t* protocol)
{
    if (!client || !protocol) {
        errno = (EINVAL);
        return -1;
    }
    
    gracht_list_remove(&client->protocols, &protocol->header);
    return 0;
}

int gracht_client_shutdown(gracht_client_t* client)
{
    if (!client) {
        errno = (EINVAL);
        return -1;
    }
    
    client->ops->destroy(client->ops);
    free(client);
    return 0;
}
