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
#include <inet/socket.h>
#include <io_events.h>
#include <io.h>
#include "include/gracht/connection.h"
#include "include/gracht/list.h"
#include "include/gracht/server.h"
#include <stdlib.h>
#include <string.h>

#define __TRACE
#include <ddk/utils.h>

typedef void (*gracht_invoke00_t)(int);
typedef void (*gracht_invokeA0_t)(int, void*);
typedef void (*gracht_invoke0R_t)(int, void*);
typedef void (*gracht_invokeAR_t)(int, void*, void*);

struct gracht_server {
    gracht_server_configuration_t configuration;
    int                       initialized;
    int                       client_socket;
    int                       dgram_socket;
    int                       socket_set;
    struct gracht_list            protocols;
} gracht_server_context = { { { 0 } } };


static int create_client_socket(gracht_server_configuration_t* configuration)
{
    int status;
    
    gracht_server_context.client_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (gracht_server_context.client_socket < 0) {
        return -1;
    }
    
    status = bind(gracht_server_context.client_socket, sstosa(&configuration->server_address),
        configuration->server_address_length);
    if (status) {
        return -1;
    }
    
    // Enable listening for connections, with a maximum of 2 on backlog
    status = listen(gracht_server_context.client_socket, 2);
    if (status) {
        return -1;
    }
    
    // Listen for control events only, there is no input/output data on the 
    // connection socket
    status = io_set_ctrl(gracht_server_context.socket_set, IO_EVT_DESCRIPTOR_ADD,
        gracht_server_context.client_socket, IOEVTCTL);
    return status;
}

static int handle_client_socket(void)
{
    struct sockaddr_storage client_address;
    socklen_t               client_address_length;
    int                     client_socket;
    int                     status;
    
    // TODO handle disconnects in accept in netmanager
    client_socket = accept(gracht_server_context.client_socket, sstosa(&client_address), &client_address_length);
    if (client_socket < 0) {
        return -1;
    }
    
    status = gracht_connection_create(client_socket, &client_address, client_address_length);
    if (status < 0) {
        return -1;
    }
    
    // We specifiy the IOEVTFRT due to race conditioning that is possible when
    // accepting new sockets. If the client is quick to send data we might miss the
    // event. So specify the INITIAL_EVENT flag to recieve an initial event
    status = io_set_ctrl(gracht_server_context.socket_set, IO_EVT_DESCRIPTOR_ADD,
        client_socket, IOEVTIN | IOEVTCTL | IOEVTFRT);
    return status;
}

static int create_dgram_socket(gracht_server_configuration_t* configuration)
{
    int status;
    
    // Create a new socket for listening to events. They are all
    // delivered to fixed sockets on the local system.
    gracht_server_context.dgram_socket = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (gracht_server_context.dgram_socket < 0) {
        return -1;
    }
    
    status = bind(gracht_server_context.dgram_socket, sstosa(&configuration->dgram_address),
        configuration->dgram_address_length);
    if (status) {
        return -1;
    }
    
    // Listen for input events on the dgram socket
    status = io_set_ctrl(gracht_server_context.socket_set, IO_EVT_DESCRIPTOR_ADD,
        gracht_server_context.dgram_socket, IOEVTIN);
    return status;
}

static gracht_protocol_function_t* get_protocol_action(uint8_t protocol_id, uint8_t action_id)
{
    gracht_protocol_t* protocol = (struct gracht_protocol*)gracht_list_lookup(
        &gracht_server_context.protocols, (int)(uint32_t)protocol_id);
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

static int invoke_action(int socket, gracht_message_t* message, void* argument_buffer,
    gracht_protocol_function_t* function, struct sockaddr_storage* client_address)
{
    int has_argument = message->length > sizeof(gracht_message_t);
    int has_return   = message->ret_length > 0;
    TRACE("[invoke_action] %u, %u", message->protocol, message->action);
    
    if (has_argument && has_return) {
        uint8_t return_buffer[message->ret_length];
        ((gracht_invokeAR_t)function->address)(socket, argument_buffer, &return_buffer[0]);
        return gracht_connection_send_reply(socket, &return_buffer[0], 
            message->ret_length, client_address);
    }
    else if (has_argument) {
        ((gracht_invokeA0_t)function->address)(socket, argument_buffer);
    }
    else if (has_return) {
        uint8_t return_buffer[message->ret_length];
        ((gracht_invoke0R_t)function->address)(socket, &return_buffer[0]);
        return gracht_connection_send_reply(socket, &return_buffer[0], 
            message->ret_length, client_address);
    }
    else {
        ((gracht_invoke00_t)function->address)(socket);
    }
    return 0;
}

static int handle_sync_event(int socket, uint32_t events, void* argument_buffer)
{
    gracht_protocol_function_t* function;
    struct sockaddr_storage client_address;
    gracht_message_t            message;
    int                     status;
    TRACE("[handle_sync_event] %i, 0x%x", socket, events);
    
    status = gracht_connection_recv_packet(socket, &message, argument_buffer, &client_address);
    if (status) {
        ERROR("[handle_sync_event] gracht_connection_recv_message returned %i", errno);
        return -1;
    }
    
    function = get_protocol_action(message.protocol, message.action);
    if (!function) {
        ERROR("[handle_sync_event] get_protocol_action returned null");
        _set_errno(ENOENT);
        return -1;
    }
    return invoke_action(socket, &message, argument_buffer, function, &client_address);
}

static int handle_async_event(int socket, uint32_t events, void* argument_buffer)
{
    gracht_protocol_function_t* function;
    gracht_message_t            message;
    int                     status;
    TRACE("[handle_async_event] %i, 0x%x", socket, events);
    
    // Check for control event. On non-passive sockets, control event is the
    // disconnect event.
    if (events & IOEVTCTL) {
        status = io_set_ctrl(gracht_server_context.socket_set, IO_EVT_DESCRIPTOR_DEL,
            socket, 0);
        if (status) {
            // TODO log
        }
        
        status = gracht_connection_shutdown(socket);
    }
    else if ((events & IOEVTIN) || !events) {
        status = gracht_connection_recv_stream(socket, &message, argument_buffer);
        
        if (status) {
            ERROR("[handle_async_event] gracht_connection_recv_message returned %i", errno);
            return -1;
        }
        
        function = get_protocol_action(message.protocol, message.action);
        if (!function) {
            ERROR("[handle_async_event] get_protocol_action returned null");
            _set_errno(ENOENT);
            return -1;
        }
        return invoke_action(socket, &message, argument_buffer, function, NULL);
    }
    return 0;
}

int gracht_server_initialize(gracht_server_configuration_t* configuration)
{
    int status;
    
    assert(gracht_server_context.initialized == 0);
    
    // store handler
    gracht_server_context.initialized = 1;
    memcpy(&gracht_server_context.configuration, configuration, 
        sizeof(gracht_server_configuration_t));
    
    // initialize connection library
    status = gracht_connection_initialize();
    if (status) {
        return status;
    }
    
    // create the io event set, for async io
    gracht_server_context.socket_set = io_set_create(0);
    if (gracht_server_context.socket_set == -1) {
        return -1;
    }
    
    status = create_client_socket(configuration);
    if (status) {
        return status;
    }
    
    status = create_dgram_socket(configuration);
    if (status) {
        return status;
    }
    
    return status;
}

static int gracht_server_shutdown(void)
{
    assert(gracht_server_context.initialized == 1);
    
    if (gracht_server_context.client_socket != -1) {
        close(gracht_server_context.client_socket);
    }
    
    if (gracht_server_context.dgram_socket != -1) {
        close(gracht_server_context.dgram_socket);
    }
    
    if (gracht_server_context.socket_set != -1) {
        close(gracht_server_context.socket_set);
    }
    
    return 0;
}

int gracht_server_main_loop(void)
{
    void*           argument_buffer;
    struct io_event events[32];
    int             i;
    
    argument_buffer = malloc(GRACHT_MAX_MESSAGE_SIZE);
    if (!argument_buffer) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    while (gracht_server_context.initialized) {
        int num_events = io_set_wait(gracht_server_context.socket_set, &events[0], 32, 0);
        for (i = 0; i < num_events; i++) {
            if (events[i].iod == gracht_server_context.client_socket) {
                if (handle_client_socket()) {
                    // TODO - log
                }
            }
            else if (events[i].iod == gracht_server_context.dgram_socket) {
                handle_sync_event(gracht_server_context.dgram_socket, events[i].events, argument_buffer);
            }
            else {
                handle_async_event(events[i].iod, events[i].events, argument_buffer);
            }
        }
    }
    
    free(argument_buffer);
    return gracht_server_shutdown();
}

int gracht_server_send_event(int client, uint8_t protocol_id, uint8_t event_id, void* argument, size_t argument_length)
{
    gracht_message_t message = {
        .length     = (sizeof(gracht_message_t) + argument_length),
        .ret_length = 0,
        .crc        = 0,
        .protocol   = protocol_id,
        .action     = event_id
    };
    return gracht_connection_send_stream(client, &message, argument, argument_length);
}

int gracht_server_broadcast_event(uint8_t protocol_id, uint8_t event_id, void* argument, size_t argument_length)
{
    gracht_message_t message = {
        .length     = (sizeof(gracht_message_t) + argument_length),
        .ret_length = 0,
        .crc        = 0,
        .protocol   = protocol_id,
        .action     = event_id
    };
    return gracht_connection_broadcast_message(&message, argument, argument_length);
}

int gracht_server_register_protocol(gracht_protocol_t* protocol)
{
    if (!protocol) {
        _set_errno(EINVAL);
        return -1;
    }
    
    gracht_list_append(&gracht_server_context.protocols, &protocol->header);
    return 0;
}

int gracht_server_unregister_protocol(gracht_protocol_t* protocol)
{
    if (!protocol) {
        _set_errno(EINVAL);
        return -1;
    }
    
    gracht_list_remove(&gracht_server_context.protocols, &protocol->header);
    return 0;
}
