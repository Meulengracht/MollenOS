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

#include <errno.h>
#include "include/gracht/aio.h"
#include "include/gracht/debug.h"
#include "include/gracht/list.h"
#include "include/gracht/server.h"
#include "include/gracht/link/link.h"
#include <stdlib.h>
#include <string.h>
#include <threads.h>

GRACHT_STRUCT(gracht_subscription_args, {
    uint8_t protocol_id;
});

static void gracht_control_subscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args*);
static void gracht_control_unsubscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args*);

static gracht_protocol_function_t control_functions[2] = {
   { 0 , gracht_control_subscribe_callback },
   { 1 , gracht_control_unsubscribe_callback },
};
static gracht_protocol_t control_protocol = GRACHT_PROTOCOL_INIT(0, "gctrl", 2, control_functions);

extern int server_invoke_action(struct gracht_list*, struct gracht_recv_message*);

struct gracht_server {
    struct server_link_ops*        ops;
    struct gracht_server_callbacks callbacks;
    void*                          messageBuffer;
    int                            initialized;
    int                            set_iod;
    int                            set_iod_provided;
    int                            client_iod;
    int                            dgram_iod;
    mtx_t                          sync_object;
    struct gracht_list             protocols;
    struct gracht_list             clients;
} g_grachtServer = {
        NULL,
        { NULL, NULL },
        NULL,
        0,
        -1,
        0,
        -1,
        -1,
        MUTEX_INIT(mtx_plain),
        { 0 },
        { 0 }
};

static void client_destroy(struct gracht_server_client*);
static void client_subscribe(struct gracht_server_client*, uint8_t);
static void client_unsubscribe(struct gracht_server_client*, uint8_t);
static int  client_is_subscribed(struct gracht_server_client*, uint8_t);

int gracht_server_initialize(gracht_server_configuration_t* configuration)
{
    if (g_grachtServer.initialized) {
        errno = EALREADY;
        return -1;
    }

    if (!configuration) {
        errno = EINVAL;
        return -1;
    }

    g_grachtServer.initialized   = 1;
    g_grachtServer.ops           = configuration->link;
    g_grachtServer.messageBuffer = malloc(GRACHT_MAX_MESSAGE_SIZE);
    if (!g_grachtServer.messageBuffer) {
        errno = ENOMEM;
        return -1;
    }

    // copy the callbacks
    memcpy(&g_grachtServer.callbacks, &configuration->callbacks, sizeof(struct gracht_server_callbacks));
    
    // create the io event set, for async io
    if (configuration->set_descriptor_provided) {
        g_grachtServer.set_iod          = configuration->set_descriptor;
        g_grachtServer.set_iod_provided = 1;
    }
    else {
        g_grachtServer.set_iod = gracht_aio_create();
        if (g_grachtServer.set_iod < 0) {
            ERROR("gracht_server: failed to create aio handle\n");
            return -1;
        }
    }
    
    // try to create the listening link. We do support that one of the links
    // are not supported by the link operations.
    g_grachtServer.client_iod = g_grachtServer.ops->listen(g_grachtServer.ops, LINK_LISTEN_SOCKET);
    if (g_grachtServer.client_iod < 0) {
        if (errno != ENOTSUP) {
            return -1;
        }
    }
    else {
        gracht_aio_add(g_grachtServer.set_iod, g_grachtServer.client_iod);
    }

    g_grachtServer.dgram_iod = g_grachtServer.ops->listen(g_grachtServer.ops, LINK_LISTEN_DGRAM);
    if (g_grachtServer.dgram_iod < 0) {
        if (errno != ENOTSUP) {
            return -1;
        }
    }
    else {
        gracht_aio_add(g_grachtServer.set_iod, g_grachtServer.dgram_iod);
    }

    if (g_grachtServer.client_iod < 0 && g_grachtServer.dgram_iod < 0) {
        ERROR("gracht_server_initialize: neither of client and dgram links were supported");
        return -1;
    }
    
    gracht_server_register_protocol(&control_protocol);
    return 0;
}

static int handle_client_socket(void)
{
    struct gracht_server_client* client;

    int status = g_grachtServer.ops->accept(g_grachtServer.ops, &client);
    if (status) {
        ERROR("gracht_server: failed to accept client\n");
        return status;
    }
    
    gracht_list_append(&g_grachtServer.clients, &client->header);
    gracht_aio_add(g_grachtServer.set_iod, client->iod);

    // invoke the new client callback at last
    if (g_grachtServer.callbacks.clientConnected) {
        g_grachtServer.callbacks.clientConnected(client->iod);
    }
    return 0;
}

static int handle_sync_event(void* storage)
{
    struct gracht_recv_message message = { .storage = storage };
    int                        status;
    TRACE("[handle_sync_event]");
    
    while (1) {
        status = g_grachtServer.ops->recv_packet(g_grachtServer.ops, &message, MSG_DONTWAIT);
        if (status) {
            if (errno != ENODATA) {
                ERROR("[handle_sync_event] server_object.ops->recv_packet returned %i\n", errno);
            }
            break;
        }
        status = server_invoke_action(&g_grachtServer.protocols, &message);
    }
    
    return status;
}

static int handle_async_event(int iod, uint32_t events, void* storage)
{
    int                          status;
    struct gracht_recv_message   message = { .storage = storage };
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, iod);
    TRACE("[handle_async_event] %i, 0x%x\n", iod, events);
    
    // Check for control event. On non-passive sockets, control event is the
    // disconnect event.
    if (events & GRACHT_AIO_EVENT_DISCONNECT) {
        status = gracht_aio_remove(g_grachtServer.set_iod, iod);
        if (status) {
            // TODO log
        }
        
        client_destroy(client);
    }
    else if ((events & GRACHT_AIO_EVENT_IN) || !events) {
        while (1) {
            status = g_grachtServer.ops->recv_client(client, &message, MSG_DONTWAIT);
            if (status) {
                if (errno != ENODATA) {
                    ERROR("[handle_async_event] server_object.ops->recv_client returned %i\n", errno);
                }
                break;
            }

            status = server_invoke_action(&g_grachtServer.protocols, &message);
            if (status) {
                WARNING("[handle_async_event] failed to invoke server action\n");
            }
        }
    }
    return 0;
}

static int gracht_server_shutdown(void)
{
    struct gracht_server_client* client;
    
    if (!g_grachtServer.initialized) {
        errno = ENOTSUP;
        return -1;
    }
    
    client = (struct gracht_server_client*)g_grachtServer.clients.head;
    while (client) {
        struct gracht_server_client* temp = (struct gracht_server_client*)client->header.link;
        client_destroy(client);
        client = temp;
    }
    g_grachtServer.clients.head = NULL;
    
    if (g_grachtServer.set_iod != -1 && !g_grachtServer.set_iod_provided) {
        gracht_aio_destroy(g_grachtServer.set_iod);
    }

    if (g_grachtServer.messageBuffer) {
        free(g_grachtServer.messageBuffer);
    }
    
    if (g_grachtServer.ops != NULL) {
        g_grachtServer.ops->destroy(g_grachtServer.ops);
    }

    g_grachtServer.initialized = 0;
    return 0;
}

int gracht_server_handle_event(int iod, unsigned int events)
{
    if (iod == g_grachtServer.client_iod) {
        return handle_client_socket();
    }
    else if (iod == g_grachtServer.dgram_iod) {
        return handle_sync_event(g_grachtServer.messageBuffer);
    }
    else {
        return handle_async_event(iod, events, g_grachtServer.messageBuffer);
    }
}

int gracht_server_main_loop(void)
{
    gracht_aio_event_t events[32];
    int                i;

    TRACE("gracht_server: started... [%i, %i]\n", g_grachtServer.client_iod, g_grachtServer.dgram_iod);
    while (g_grachtServer.initialized) {
        int num_events = gracht_io_wait(g_grachtServer.set_iod, &events[0], 32);
        TRACE("gracht_server: %i events received!\n", num_events);
        for (i = 0; i < num_events; i++) {
            int      iod   = gracht_aio_event_iod(&events[i]);
            uint32_t flags = gracht_aio_event_events(&events[i]);

            TRACE("gracht_server: event %u from %i\n", flags, iod);
            gracht_server_handle_event(iod, flags);
        }
    }

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

    // update the id for the response
    message->header.id = messageContext->message_id;

    client = (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, messageContext->client);
    if (!client) {
        return g_grachtServer.ops->respond(g_grachtServer.ops, messageContext, message);
    }

    return g_grachtServer.ops->send_client(client, message, MSG_WAITALL);
}

int gracht_server_send_event(int client, struct gracht_message* message, unsigned int flags)
{
    struct gracht_server_client* serverClient = 
        (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, client);
    if (!serverClient) {
        errno = (ENOENT);
        return -1;
    }
    
    // When sending target specific events - we do not care about subscriptions
    return g_grachtServer.ops->send_client(serverClient, message, flags);
}

int gracht_server_broadcast_event(struct gracht_message* message, unsigned int flags)
{
    struct gracht_server_client* client;
    
    client = (struct gracht_server_client*)g_grachtServer.clients.head;
    while (client) {
        if (client_is_subscribed(client, message->header.protocol)) {
            g_grachtServer.ops->send_client(client, message, flags);
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
    
    gracht_list_append(&g_grachtServer.protocols, &protocol->header);
    return 0;
}

int gracht_server_unregister_protocol(gracht_protocol_t* protocol)
{
    if (!protocol) {
        errno = (EINVAL);
        return -1;
    }
    
    gracht_list_remove(&g_grachtServer.protocols, &protocol->header);
    return 0;
}

int gracht_server_get_dgram_iod(void)
{
    return g_grachtServer.dgram_iod;
}

int gracht_server_get_set_iod(void)
{
    return g_grachtServer.set_iod;
}

// Client helpers
static void client_destroy(struct gracht_server_client* client)
{
    if (g_grachtServer.callbacks.clientDisconnected) {
        g_grachtServer.callbacks.clientDisconnected(client->iod);
    }

    gracht_list_remove(&g_grachtServer.clients, &client->header);
    g_grachtServer.ops->destroy_client(client);
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
void gracht_control_subscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args* input)
{
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, message->client);
    if (!client) {
        if (g_grachtServer.ops->create_client(g_grachtServer.ops, message, &client)) {
            ERROR("[gracht_control_subscribe_callback] server_object.ops->create_client returned error");
            return;
        }
        gracht_list_append(&g_grachtServer.clients, &client->header);

        if (g_grachtServer.callbacks.clientConnected) {
            g_grachtServer.callbacks.clientConnected(client->iod);
        }
    }

    client_subscribe(client, input->protocol_id);
}

void gracht_control_unsubscribe_callback(struct gracht_recv_message* message, struct gracht_subscription_args* input)
{
    struct gracht_server_client* client = 
        (struct gracht_server_client*)gracht_list_lookup(&g_grachtServer.clients, message->client);
    if (!client) {
        return;
    }

    client_unsubscribe(client, input->protocol_id);
    
    // cleanup the client if we unsubscripe
    if (input->protocol_id == 0xFF) {
        client_destroy(client);
    }
}
