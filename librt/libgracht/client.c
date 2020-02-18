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
#include <inet/socket.h>
#include <io.h>
#include "include/gracht/client.h"
#include "include/gracht/connection.h"
#include "include/gracht/crc.h"
#include "include/gracht/list.h"
#include <signal.h>
#include <string.h>
#include <stdlib.h>

typedef struct gracht_client {
    uint32_t            client_id;
    enum gracht_client_type type;
    int                 event_loop_enabled;
    int                 socket;
    struct gracht_list      protocols;
} gracht_client_t;

typedef void (*gracht_invoke00_t)(void);
typedef void (*gracht_invokeA0_t)(void*);

static int get_message_reply(gracht_client_t* client, void* return_buffer, 
    size_t return_length)
{
    intmax_t bytes_read = recv(client->socket, return_buffer, 
        return_length, MSG_WAITALL);
    return bytes_read != return_length;
}

int gracht_client_invoke(gracht_client_t* client, uint8_t protocol, uint8_t action, 
    void* arguments, size_t argument_length, void* return_buffer, 
    size_t return_length)
{
    int          status;
    gracht_message_t message = { 
        .length     = (sizeof(gracht_message_t) + argument_length),
        .ret_length = return_length,
        .crc        = 0,
        .protocol   = protocol,
        .action     = action
    };
    
    if (argument_length) {
        message.crc = crc16_generate((const unsigned char*)arguments, argument_length);
    }
    
    switch (client->type) {
        case gracht_client_stream_based: {
            if (return_buffer != NULL) {
                _set_errno(ENOTSUP);
                return -1;
            }
            status = gracht_connection_send_stream(client->socket, &message, arguments, argument_length);
        } break;
        case gracht_client_packet_based: {
            status = gracht_connection_send_packet(client->socket, &message, arguments, argument_length, NULL);
            if (!status && argument_length) {
                status = get_message_reply(client, return_buffer, return_length);
            }
        } break;
    }
    
    return status;
}

static gracht_protocol_function_t* get_protocol_action(gracht_client_t* client, uint8_t protocol_id, uint8_t action_id)
{
    gracht_protocol_t* protocol = (struct gracht_protocol*)gracht_list_lookup(&client->protocols, (int)(uint32_t)protocol_id);
    int            i;
    
    if (!protocol) {
        return NULL;
    }
    
    for (i = 0; i < protocol->num_functions; i++) {
        if (protocol->functions[i].id == action_id) {
            return &protocol->functions[i];
        }
    }
    return NULL;
}

static void invoke_action(gracht_message_t* message, void* argument_buffer, gracht_protocol_function_t* function)
{
    if (message->length > sizeof(gracht_message_t)) {
        ((gracht_invokeA0_t)function->address)(argument_buffer);
    }
    else {
        ((gracht_invoke00_t)function->address)();
    }
}

int gracht_client_event_loop(gracht_client_t* client)
{
    void*        argument_buffer;
    gracht_message_t message;
    
    if (!client) {
        _set_errno(EINVAL);
        return -1;
    }
    
    argument_buffer = malloc(GRACHT_MAX_MESSAGE_SIZE);
    if (!argument_buffer) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    client->event_loop_enabled = 1;
    while (client->event_loop_enabled) {
        int status = -1;
        switch (client->type) {
            case gracht_client_stream_based: {
                status = gracht_connection_recv_stream(client->socket, &message, argument_buffer);
            } break;
            case gracht_client_packet_based: {
                status = gracht_connection_recv_packet(client->socket, &message, argument_buffer, NULL);
            } break;
        }
        
        if (!status) {
            gracht_protocol_function_t* function = get_protocol_action(client, message.protocol, message.action);
            if (function) {
                invoke_action(&message, argument_buffer, function);
            }
        }
    }
    
    free(argument_buffer);
    return 0;
}

int gracht_client_stop_event_loop(gracht_client_t* client)
{
    if (!client) {
        _set_errno(EINVAL);
        return -1;
    }
    
    client->event_loop_enabled = 0;
    return 0;
}

static int create_stream_socket(gracht_client_configuration_t* config)
{
    int fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (fd == -1) {
        return -1;
    }
    
    int status = connect(fd, sstosa(&config->address), config->address_length);
    if (status) {
        close(fd);
        return status;
    }
    return fd;
}

static int create_packet_socket(gracht_client_configuration_t* config)
{
    int fd = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (fd == -1) {
        return -1;
    }
    
    int status = connect(fd, sstosa(&config->address), config->address_length);
    if (status) {
        close(fd);
        return status;
    }
    return fd;
}

int gracht_client_create(gracht_client_configuration_t* config, gracht_client_t** client_out)
{
    gracht_client_t* client;
    
    client = (gracht_client_t*)malloc(sizeof(gracht_client_t));
    if (!client) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    memset(client, 0, sizeof(gracht_client_t));
    client->type = config->type;
    switch (config->type) {
        case gracht_client_stream_based: {
            client->socket = create_stream_socket(config);
        } break;
        case gracht_client_packet_based: {
            client->socket = create_packet_socket(config);
        } break;
    }
    
    if (client->socket == -1) {
        free(client);
        return -1;
    }
    
    *client_out = client;
    return 0;
}

int gracht_client_register_protocol(gracht_client_t* client, gracht_protocol_t* protocol)
{
    if (!client || !protocol) {
        _set_errno(EINVAL);
        return -1;
    }
    
    gracht_list_append(&client->protocols, &protocol->header);
    return 0;
}

int gracht_client_unregister_protocol(gracht_client_t* client, gracht_protocol_t* protocol)
{
    if (!client || !protocol) {
        _set_errno(EINVAL);
        return -1;
    }
    
    gracht_list_remove(&client->protocols, &protocol->header);
    return 0;
}

int gracht_client_shutdown(gracht_client_t* client)
{
    if (!client) {
        _set_errno(EINVAL);
        return -1;
    }
    
    close(client->socket);
    free(client);
    return 0;
}

