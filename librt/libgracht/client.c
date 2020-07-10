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
#include "include/gracht/threads.h"
#include <string.h>
#include <stdlib.h>

struct gracht_message_awaiter {
    struct gracht_object_header header;
    unsigned int                flags;
    cnd_t                       event;
    int                         id_count;
    uint32_t                    ids[];
};

// descriptor | message | params
struct gracht_message_descriptor {
    gracht_object_header_t header;
    int                    status;
    struct gracht_message  message;
};

typedef struct gracht_client {
    int                     iod;
    uint32_t                current_message_id;
    struct client_link_ops* ops;
    struct gracht_list      protocols;
    struct gracht_list      awaiters;
    struct gracht_list      messages;
    mtx_t                   sync_object;
    mtx_t                   wait_object;
} gracht_client_t;

// static methods
static uint32_t get_message_id(gracht_client_t*);
static void     mark_awaiters(gracht_client_t*, uint32_t);
static int      check_awaiter_condition(gracht_client_t*, struct gracht_message_awaiter*, struct gracht_message_context**, int);

// extern methods
extern int client_invoke_action(struct gracht_list*, struct gracht_message*);

// allocated => list_header, message_id, output_buffer
int gracht_client_invoke(gracht_client_t* client, struct gracht_message_context* context,
    struct gracht_message* message)
{
    struct gracht_message_descriptor* descriptor = NULL;
    int status;
    
    if (!client || !message) {
        errno = (EINVAL);
        return -1;
    }
    
    // fill in some message details
    message->header.id = get_message_id(client);
    
    // require intermediate buffer for sync operations
    if (MESSAGE_FLAG_TYPE(message->header.flags) == MESSAGE_FLAG_SYNC) {
        size_t bufferLength = sizeof(struct gracht_message_descriptor) + (message->header.param_out * sizeof(struct gracht_param));
        int    i;

        if (!context) {
            errno = EINVAL;
            return -1;
        }

        for (i = 0; i < message->header.param_out; i++) {
            if (message->params[message->header.param_in + i].type == GRACHT_PARAM_BUFFER) {
                bufferLength += message->params[message->header.param_in + i].length;
            }
        }
        
        context->message_id = message->header.id;
        context->descriptor = malloc(bufferLength);
        if (!context->descriptor) {
            errno = ENOMEM;
            return -1;
        }
        
        descriptor = context->descriptor;
        descriptor->header.id   = (int)message->header.id;
        descriptor->header.link = NULL;
        descriptor->status      = GRACHT_MESSAGE_CREATED;
        
        mtx_lock(&client->sync_object);
        gracht_list_append(&client->messages, &descriptor->header);
        mtx_unlock(&client->sync_object);
    }
    
    status = client->ops->send(client->ops, message, context);
    if (descriptor) {
        descriptor->status = status;
    }
    return status == GRACHT_MESSAGE_ERROR ? -1 : 0;
}

int gracht_client_await_multiple(gracht_client_t* client,
    struct gracht_message_context** contexts, int contextCount, unsigned int flags)
{
    struct gracht_message_awaiter* awaiter;
    int                            i;
    
    if (!client || !contexts || !contextCount) {
        errno = (EINVAL);
        return -1;
    }
    
    awaiter = malloc(sizeof(struct gracht_message_awaiter) + (sizeof(uint32_t) * contextCount));
    if (!awaiter) {
        errno = (ENOMEM);
        return -1;
    }
    
    cnd_init(&awaiter->event);
    awaiter->header.id   = 0;
    awaiter->header.link = NULL;
    awaiter->flags       = flags;
    awaiter->id_count    = contextCount;
    for (i = 0; i < contextCount; i++) {
        awaiter->ids[i] = contexts[i]->message_id;
    }
    
    // do not add the awaiter if the condition is success
    mtx_lock(&client->sync_object);
    if (check_awaiter_condition(client, awaiter, contexts, contextCount)) {
        gracht_list_append(&client->awaiters, &awaiter->header);
        cnd_wait(&awaiter->event, &client->sync_object);
        gracht_list_remove(&client->awaiters, &awaiter->header);
    }
    mtx_unlock(&client->sync_object);
    
    free(awaiter);
    return 0;
}

int gracht_client_await(gracht_client_t* client, struct gracht_message_context* context)
{
    return gracht_client_await_multiple(client, &context, 1, GRACHT_AWAIT_ANY);
}

// status, output_buffer
int gracht_client_status(gracht_client_t* client, struct gracht_message_context* context,
    struct gracht_param* params)
{
    struct gracht_message_descriptor* descriptor;
    char*                             pointer = NULL;
    int                               status;
    int                               i;
    TRACE("[gracht] [client] get status from context\n");
    
    if (!client || !context || !params) {
        errno = (EINVAL);
        return -1;
    }
    
    // guard against already checked
    mtx_lock(&client->sync_object);
    descriptor = (struct gracht_message_descriptor*)context->descriptor;
    if (!descriptor) {
        mtx_unlock(&client->sync_object);

        ERROR("[gracht] [client] descriptor for message was not found\n");
        errno = (EALREADY);
        return -1;
    }
    
    if (descriptor->status == GRACHT_MESSAGE_COMPLETED || 
        descriptor->status == GRACHT_MESSAGE_ERROR) {
        gracht_list_remove(&client->messages, &descriptor->header);
        pointer = (char*)&descriptor->message.params[descriptor->message.header.param_in];
    }
    mtx_unlock(&client->sync_object);

    status = descriptor->status;
    if (pointer) {
        TRACE("[gracht] [client] unpacking parameters\n");
        for (i = 0; i < descriptor->message.header.param_in; i++) {
            struct gracht_param* out_param = &params[i];
            struct gracht_param* in_param  = &descriptor->message.params[i];
            
            if (out_param->data.buffer && out_param->type == GRACHT_PARAM_VALUE) {
                if (out_param->length == 1) {
                    *((uint8_t*)out_param->data.buffer) = (uint8_t)(in_param->data.value & 0xFF);
                }
                else if (out_param->length == 2) {
                    *((uint16_t*)out_param->data.buffer) = (uint16_t)(in_param->data.value & 0xFFFF);
                }
                else if (out_param->length == 4) {
                    *((uint32_t*)out_param->data.buffer) = (uint32_t)(in_param->data.value & 0xFFFFFFFF);
                }
                else if (out_param->length == 8) {
                    *((uint64_t*)out_param->data.buffer) = (uint64_t)in_param->data.value;
                }
            }
            else if (out_param->type == GRACHT_PARAM_BUFFER) {
                if (out_param->data.buffer) {
                    memcpy(out_param->data.buffer, pointer, in_param->length);
                }
                pointer += in_param->length;
            }
        }
        
        free(context->descriptor);
    }
    
    return status;
}

int gracht_client_wait_message(
        gracht_client_t*               client,
        struct gracht_message_context* context,
        void*                          messageBuffer,
        unsigned int                   flags)
{
    struct gracht_message* message;
    int                    status;
    
    if (!client || !messageBuffer) {
        errno = (EINVAL);
        return -1;
    }

    if (mtx_trylock(&client->wait_object) != thrd_success) {
        if (!(flags & GRACHT_WAIT_BLOCK)) {
            errno = EBUSY;
            return -1;
        }

        // did not succeed in taking the lock, if we are waiting for a specific message, then we do an
        // await instead
        if (context) {
            return gracht_client_await(client, context);
        }
        else {
            // otherwise we are trying to just wait for events, and another thread is already doing that
            // should we just wait? For now accept the fate that we wait
            if (mtx_lock(&client->wait_object) != thrd_success) {
                return -1;
            }
        }
    }

    status = client->ops->recv(client->ops, messageBuffer, flags, &message);
    mtx_unlock(&client->wait_object);
    if (status) {
        return status;
    }
    
    // if the message is not an event, then do not invoke any actions
    TRACE("[gracht] [client] invoking message type %u - %u/%u",
        message->header.flags, message->header.protocol, message->header.action);
    if (MESSAGE_FLAG_TYPE(message->header.flags) == MESSAGE_FLAG_EVENT) {
        return client_invoke_action(&client->protocols, message);
    }
    else if (MESSAGE_FLAG_TYPE(message->header.flags) == MESSAGE_FLAG_RESPONSE) {
        struct gracht_message_descriptor* descriptor = (struct gracht_message_descriptor*)
            gracht_list_lookup(&client->messages, (int)message->header.id);
        if (!descriptor) {
            // what the heck?
            ERROR("[gracht_client_wait_message] descriptor %u was not found", message->header.id);
            return -1;
        }
        
        // copy data over to message
        memcpy(&descriptor->message, message, message->header.length);
        
        // set status
        descriptor->status = GRACHT_MESSAGE_COMPLETED;
        
        // iterate awaiters and mark those that contain this message
        mtx_lock(&client->sync_object);
        mark_awaiters(client, message->header.id);
        mtx_unlock(&client->sync_object);
    }
    return 0;
}

int gracht_client_create(gracht_client_configuration_t* config, gracht_client_t** clientOut)
{
    gracht_client_t* client;
    
    if (!config || !config->link || !clientOut) {
        ERROR("[gracht] [client] config or config link was null");
        errno = EINVAL;
        return -1;
    }
    
    client = (gracht_client_t*)malloc(sizeof(gracht_client_t));
    if (!client) {
        ERROR("gracht_client: failed to allocate memory for client data\n");
        errno = (ENOMEM);
        return -1;
    }
    
    memset(client, 0, sizeof(gracht_client_t));
    mtx_init(&client->sync_object, mtx_plain);
    mtx_init(&client->wait_object, mtx_plain);

    client->ops = config->link;
    client->iod = client->ops->connect(client->ops);
    if (client->iod < 0) {
        ERROR("gracht_client: failed to connect client\n");
        gracht_client_shutdown(client);
        return -1;
    }
    
    *clientOut = client;
    return 0;
}

int gracht_client_shutdown(gracht_client_t* client)
{
    if (!client) {
        errno = (EINVAL);
        return -1;
    }
    
    if (client->iod > 0) {
        client->ops->destroy(client->ops);
    }
    
    mtx_destroy(&client->sync_object);
    mtx_destroy(&client->wait_object);
    free(client);
    return 0;
}

int gracht_client_iod(gracht_client_t* client)
{
    if (!client) {
        errno = EINVAL;
        return -1;
    }
    return client->iod;
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

static void mark_awaiters(gracht_client_t* client, uint32_t messageId)
{
    struct gracht_object_header* item;
    
    item = GRACHT_LIST_HEAD(&client->awaiters);
    while (item) {
        struct gracht_message_awaiter* awaiter = 
            (struct gracht_message_awaiter*)item;
        int idsLeft = awaiter->id_count;
        int i;
        
        for (i = 0; i < awaiter->id_count; i++) {
            if (awaiter->ids[i] == 0) {
                idsLeft--;
                continue;
            }
            
            if (awaiter->ids[i] == messageId) {
                awaiter->ids[i] = 0;
                idsLeft--;
            }
        }
        
        if (idsLeft != awaiter->id_count) {
            if (idsLeft == 0 || awaiter->flags == GRACHT_AWAIT_ANY) {
                cnd_signal(&awaiter->event);
            }
        }
        
        item = GRACHT_LIST_LINK(item);
    }
}

static int check_awaiter_condition(gracht_client_t* client,
    struct gracht_message_awaiter* awaiter, struct gracht_message_context** contexts,
    int contextCount)
{
    int messagesCompleted = 0;
    int i;
    
    for (i = 0; i < contextCount; i++) {
        struct gracht_message_descriptor* descriptor =  (struct gracht_message_descriptor*)
            gracht_list_lookup(&client->messages, (int)contexts[i]->message_id);
        if (descriptor && ( 
                descriptor->status == GRACHT_MESSAGE_INPROGRESS ||
                descriptor->status == GRACHT_MESSAGE_CREATED)) {
            continue;
        }
        
        messagesCompleted++;
    }
    
    if (messagesCompleted != 0) {
        if (messagesCompleted == contextCount || awaiter->flags == GRACHT_AWAIT_ANY) {
            // condition was met
            return 0;
        }
    }
    return -1;
}

static uint32_t get_message_id(gracht_client_t* client)
{
    return client->current_message_id++;
}
