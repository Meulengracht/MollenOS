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
 * Gracht Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <errno.h>
#include "include/gracht/aio.h"
#include "include/gracht/debug.h"
#include "include/gracht/list.h"
#include "include/gracht/server.h"
#include "include/gracht/link/link.h"
#include <stdlib.h>
#include <string.h>

#include <gracht_control_protocol_server.h>

extern int server_invoke_action(struct gracht_list*, struct gracht_recv_message*);

struct gracht_server {
    struct server_link_ops* ops;
    int                     initialized;
    int                     completion_iod;
    int                     client_iod;
    int                     dgram_iod;
    struct gracht_list      protocols;
    struct gracht_list      clients;
} server_object = { NULL, 0, -1, -1, -1, { 0 }, { 0 } };

static void client_destroy(struct gracht_server_client*);
static void client_subscribe(struct gracht_server_client*, uint8_t);
static void client_unsubscribe(struct gracht_server_client*, uint8_t);
static int  client_is_subscribed(struct gracht_server_client*, uint8_t);

int gracht_server_initialize(gracht_server_configuration_t* configuration)
{
    assert(server_object.initialized == 0);
    
    // store handler
    server_object.initialized = 1;
    server_object.ops = configuration->link;
    
    // create the io event set, for async io
    server_object.completion_iod = gracht_aio_create();
    if (server_object.completion_iod < 0) {
        ERROR("gracht_server: failed to create aio handle\n");
        return -1;
    }
    
    // try to create the listening link. We do support that one of the links
    // are not supported by the link operations.
    server_object.client_iod = server_object.ops->listen(server_object.ops, LINK_LISTEN_SOCKET);
    if (server_object.client_iod < 0) {
        if (errno != ENOTSUP) {
            return -1;
        }
    }
    else {
        gracht_aio_add(server_object.completion_iod, server_object.client_iod);
    }
    
    server_object.dgram_iod = server_object.ops->listen(server_object.ops, LINK_LISTEN_DGRAM);
    if (server_object.dgram_iod < 0) {
        if (errno != ENOTSUP) {
            return -1;
        }
    }
    else {
        gracht_aio_add(server_object.completion_iod, server_object.dgram_iod);
    }

    if (server_object.client_iod < 0 && server_object.dgram_iod < 0) {
        ERROR("gracht_server_initialize: neither of client and dgram links were supported");
        return -1;
    }
    
    gracht_server_register_protocol(&gracht_control_protocol);
    return 0;
}

static int handle_client_socket(void)
{
    struct gracht_server_client* client;

    int status = server_object.ops->accept(server_object.ops, &client);
    if (status) {
        ERROR("gracht_server: failed to accept client\n");
        return status;
    }
    
    gracht_list_append(&server_object.clients, &client->header);
    gracht_aio_add(server_object.completion_iod, client->iod);
    return 0;
}

static int handle_sync_event(int iod, uint32_t events, void* storage)
{
    struct gracht_recv_message message = { .storage = storage };
    int                        status;
    TRACE("[handle_sync_event] %i, 0x%x\n", iod, events);
    
    while (1) {
        status = server_object.ops->recv_packet(server_object.ops, &message, MSG_DONTWAIT);
        if (status) {
            if (errno != ENODATA) {
                ERROR("[handle_sync_event] server_object.ops->recv_packet returned %i\n", errno);
            }
            break;
        }
        status = server_invoke_action(&server_object.protocols, &message);
    }
    
    return status;
}

static int handle_async_event(int iod, uint32_t events, void* storage)
{
    int                          status;
    struct gracht_recv_message   message = { .storage = storage };
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&server_object.clients, iod);
    TRACE("[handle_async_event] %i, 0x%x\n", iod, events);
    
    // Check for control event. On non-passive sockets, control event is the
    // disconnect event.
    if (events & GRACHT_AIO_EVENT_DISCONNECT) {
        status = gracht_aio_remove(server_object.completion_iod, iod);
        if (status) {
            // TODO log
        }
        
        client_destroy(client);
    }
    else if ((events & GRACHT_AIO_EVENT_IN) || !events) {
        while (1) {
            status = server_object.ops->recv_client(client, &message, MSG_DONTWAIT);
            if (status) {
                if (errno != ENODATA) {
                    ERROR("[handle_async_event] server_object.ops->recv_client returned %i\n", errno);
                }
                break;
            }
            
            status = server_invoke_action(&server_object.protocols, &message);
        }
    }
    return 0;
}

static int gracht_server_shutdown(void)
{
    struct gracht_server_client* client;
    
    assert(server_object.initialized == 1);
    
    client = (struct gracht_server_client*)server_object.clients.head;
    while (client) {
        struct gracht_server_client* temp = (struct gracht_server_client*)client->header.link;
        client_destroy(client);
        client = temp;
    }
    server_object.clients.head = NULL;
    
    if (server_object.completion_iod != -1) {
        gracht_aio_destroy(server_object.completion_iod);
    }
    
    if (server_object.ops != NULL) {
        server_object.ops->destroy(server_object.ops);
    }
    
    server_object.initialized = 0;
    return 0;
}

int gracht_server_main_loop(void)
{
    void*              storage;
    gracht_aio_event_t events[32];
    int                i;
    
    storage = malloc(GRACHT_MAX_MESSAGE_SIZE);
    if (!storage) {
        errno = (ENOMEM);
        return -1;
    }

    TRACE("gracht_server: started... [%i, %i]\n", server_object.client_iod, server_object.dgram_iod);
    while (server_object.initialized) {
        int num_events = gracht_io_wait(server_object.completion_iod, &events[0], 32);
        TRACE("gracht_server: %i events received!\n", num_events);
        for (i = 0; i < num_events; i++) {
            int      iod   = gracht_aio_event_iod(&events[i]);
            uint32_t flags = gracht_aio_event_events(&events[i]);

            TRACE("gracht_server: event %u from %i\n", flags, iod);
            if (iod == server_object.client_iod) {
                if (handle_client_socket()) {
                    // TODO - log
                }
            }
            else if (iod == server_object.dgram_iod) {
                handle_sync_event(server_object.dgram_iod, flags, storage);
            }
            else {
                handle_async_event(iod, flags, storage);
            }
        }
    }
    
    free(storage);
    return gracht_server_shutdown();
}

int gracht_server_respond(struct gracht_recv_message* messageContext, struct gracht_message* message)
{
    struct gracht_server_client* client;

    if (!messageContext || !message) {
        ERROR("gracht_server: null message or context");
        errno = (EINVAL);
        return -1;
    }

    if (messageContext->client == server_object.dgram_iod) {
        return server_object.ops->respond(server_object.ops, messageContext, message);
    }

    client = (struct gracht_server_client*)gracht_list_lookup(&server_object.clients, messageContext->client);
    if (!client) {
        ERROR("gracht_server: failed to find client");
        errno = (ENOENT);
        return -1;
    }

    return server_object.ops->send_client(client, message, MSG_WAITALL);
}

int gracht_server_send_event(int client, struct gracht_message* message, unsigned int flags)
{
    struct gracht_server_client* serverClient = 
        (struct gracht_server_client*)gracht_list_lookup(&server_object.clients, client);
    if (!serverClient) {
        errno = (ENOENT);
        return -1;
    }
    
    // When sending target specific events - we do not care about subscriptions
    return server_object.ops->send_client(serverClient, message, flags);
}

int gracht_server_broadcast_event(struct gracht_message* message, unsigned int flags)
{
    struct gracht_server_client* client;
    
    client = (struct gracht_server_client*)server_object.clients.head;
    while (client) {
        if (client_is_subscribed(client, message->header.protocol)) {
            server_object.ops->send_client(client, message, flags);
        }
        client = (struct gracht_server_client*)client->header.link;
    }
    return 0;
}

int gracht_server_register_protocol(gracht_protocol_t* protocol)
{
    if (!protocol) {
        errno = (EINVAL);
        return -1;
    }
    
    gracht_list_append(&server_object.protocols, &protocol->header);
    return 0;
}

int gracht_server_unregister_protocol(gracht_protocol_t* protocol)
{
    if (!protocol) {
        errno = (EINVAL);
        return -1;
    }
    
    gracht_list_remove(&server_object.protocols, &protocol->header);
    return 0;
}

int gracht_server_get_dgram_iod(void)
{
    return server_object.dgram_iod;
}

// Client helpers
static void client_destroy(struct gracht_server_client* client)
{
    gracht_list_remove(&server_object.clients, &client->header);
    server_object.ops->destroy_client(client);
}

// Client subscription helpers
static void client_subscribe(struct gracht_server_client* client, uint8_t id)
{
    int block  = id / 32;
    int offset = id % 32;

    if (id == 0xFF) {
        // subscribe to all
        memset(&client->subscriptions[0], 0xFF, sizeof(client->subscriptions));
        return;
    }

    client->subscriptions[block] |= (1 << offset);
}

static void client_unsubscribe(struct gracht_server_client* client, uint8_t id)
{
    int block  = id / 32;
    int offset = id % 32;

    if (id == 0xFF) {
        // unsubscribe to all
        memset(&client->subscriptions[0], 0, sizeof(client->subscriptions));
        return;
    }

    client->subscriptions[block] &= ~(1 << offset);
}

static int client_is_subscribed(struct gracht_server_client* client, uint8_t id)
{
    int block  = id / 32;
    int offset = id % 32;
    return (client->subscriptions[block] & (1 << offset)) != 0;
}

// Server control protocol implementation
void gracht_control_get_protocols_callback(struct gracht_recv_message* message)
{
    struct gracht_protocol* protocol;
    
    protocol = (struct gracht_protocol*)server_object.protocols.head;
    while (protocol) {
        gracht_control_event_protocol_single(message->client, protocol->name, protocol->id);
        protocol = (struct gracht_protocol*)protocol->header.link;
    }
}

void gracht_control_subscribe_callback(struct gracht_recv_message* message, struct gracht_control_subscribe_args* input)
{
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&server_object.clients, message->client);
    if (!client) {
        if (server_object.ops->create_client(server_object.ops, message, &client)) {
            ERROR("[gracht_control_subscribe_callback] server_object.ops->create_client returned error");
            return;
        }
        gracht_list_append(&server_object.clients, &client->header);
    }

    client_subscribe(client, input->protocol_id);
}

void gracht_control_unsubscribe_callback(struct gracht_recv_message* message, struct gracht_control_unsubscribe_args* input)
{
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&server_object.clients, message->client);
    if (!client) {
        return;
    }

    client_unsubscribe(client, input->protocol_id);
    
    // cleanup the client if we unsubscripe
    if (input->protocol_id == 0xFF) {
        client_destroy(client);
    }
}
