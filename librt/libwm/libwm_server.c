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
 * Wm Server Type Definitions & Structures
 * - This header describes the base server-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <inet/socket.h>
#include <io_events.h>
#include <io.h>
#include "include/libwm_connection.h"
#include "include/libwm_os.h"
#include "include/libwm_server.h"
#include <stdlib.h>
#include <string.h>

typedef void (*wm_invoke00_t)(void);
typedef void (*wm_invokeA0_t)(void*);
typedef void (*wm_invoke0R_t)(void*);
typedef void (*wm_invokeAR_t)(void*, void*);

struct wm_server {
    wm_server_configuration_t configuration;
    int                       initialized;
    int                       server_socket;
    int                       input_socket;
    int                       socket_set;
    wm_protocol_t*            protocols[WM_MAX_PROTOCOLS];
} wm_server_context = { { 0 } };

#include <errno.h>
#include <ddk/utils.h>

static int create_server_socket(void)
{
    struct sockaddr_storage wm_address;
    socklen_t               wm_address_length;
    int                     status;
    
    wm_server_context.server_socket = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (wm_server_context.server_socket < 0) {
        ERROR("[create_server_socket] socket returned %i, with code %i",
            wm_server_context.server_socket, errno);
        return -1;
    }
    
    wm_os_get_server_address(&wm_address, &wm_address_length);
    status = bind(wm_server_context.server_socket, sstosa(&wm_address), wm_address_length);
    if (status) {
        ERROR("[create_server_socket] bind returned %i, with code %i", status, errno);
        return -1;
    }
    
    // Enable listening for connections, with a maximum of 2 on backlog
    status = listen(wm_server_context.server_socket, 2);
    if (status) {
        ERROR("[create_server_socket] listen returned %i, with code %i", status, errno);
    }
    return status;
}

static int handle_server_socket(void)
{
    struct sockaddr_storage client_address;
    socklen_t               client_address_length;
    int                     client_socket;
    int                     status;
    
    client_socket = accept(wm_server_context.server_socket, sstosa(&client_address), &client_address_length);
    if (client_socket < 0) {
        return -1;
    }
    
    status = wm_connection_create(client_socket, &client_address, client_address_length);
    if (status < 0) {
        return -1;
    }
    return io_set_ctrl(wm_server_context.socket_set, IO_EVT_DESCRIPTOR_ADD,
        client_socket, IOEVTIN | IOEVTOUT | IOEVTCTL);
}

static int create_input_socket(void)
{
    struct sockaddr_storage input_address;
    socklen_t               input_address_length;
    int                     status;
    
    // Create a new socket for listening to input events. They are all
    // delivered to fixed sockets on the local system.
    wm_server_context.input_socket = socket(AF_LOCAL, SOCK_DGRAM, 0);
    if (wm_server_context.input_socket < 0) {
        return -1;
    }
    
    // Connect to the input pipe
    wm_os_get_input_address(&input_address, &input_address_length);
    status = bind(wm_server_context.input_socket, sstosa(&input_address), input_address_length);
    return status;
}

static int handle_input_socket(void)
{
    wm_input_event_t input_data;
    intmax_t         bytes_read;
    
    bytes_read = recv(wm_server_context.input_socket, &input_data, sizeof(wm_input_event_t), 0);
    if (bytes_read != sizeof(wm_input_event_t)) {
        // do not process incomplete requests
        // TODO handling
        return -1;
    }
    
    // elevate key press
    wm_server_context.configuration.input_handler(&input_data);
    return 0;
}

static wm_protocol_function_t* get_protocol_action(uint8_t protocol_id, uint8_t action_id)
{
    wm_protocol_t* protocol = NULL;
    int            i;
    
    for (i = 0; i < WM_MAX_PROTOCOLS; i++) {
        if (wm_server_context.protocols[i] && 
            wm_server_context.protocols[i]->id == protocol_id) {
            protocol = wm_server_context.protocols[i];
            break;
        }
    }
    
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

static int invoke_action(int socket, wm_message_t* message, 
    void* argument_buffer, wm_protocol_function_t* function)
{
    uint8_t return_buffer[WM_MESSAGE_GET_LENGTH(message->ret_length)];
    
    if (message->has_arg && message->has_ret) {
        ((wm_invokeAR_t)function->address)(argument_buffer, &return_buffer[0]);
        return wm_connection_send_reply(socket, &return_buffer[0], 
            WM_MESSAGE_GET_LENGTH(message->ret_length));
    }
    else if (message->has_arg) {
        ((wm_invokeA0_t)function->address)(argument_buffer);
    }
    else if (message->has_ret) {
        ((wm_invoke0R_t)function->address)(&return_buffer[0]);
        return wm_connection_send_reply(socket, &return_buffer[0], 
            WM_MESSAGE_GET_LENGTH(message->ret_length));
    }
    else {
        ((wm_invoke00_t)function->address)();
    }
    return 0;
}

static int handle_client_event(int socket, void* argument_buffer)
{
    wm_protocol_function_t* function;
    wm_message_t            message;
    int                     status;
    
    status = wm_connection_recv_message(socket, &message, argument_buffer);
    if (status) {
        return -1;
    }
    
    function = get_protocol_action(message.protocol, message.action);
    if (!function) {
        _set_errno(ENOENT);
        return -1;
    }
    return invoke_action(socket, &message, argument_buffer, function);
}

int wm_server_initialize(wm_server_configuration_t* configuration)
{
    int status;
    
    assert(wm_server_context.initialized == 0);
    
    // store handler
    wm_server_context.initialized = 1;
    memcpy(&wm_server_context.configuration, configuration, 
        sizeof(wm_server_configuration_t));
    
    // initialize connection library
    status = wm_connection_initialize();
    if (status) {
        return status;
    }
    
    // create the io event set, for async io
    wm_server_context.socket_set = io_set_create(0);
    if (wm_server_context.socket_set == -1) {
        return -1;
    }
    
    // initialize default sockets
    status = create_server_socket();
    if (status) {
        return status;
    }
    
    status = create_input_socket();
    
    // register control protocol

    return status;
}

static int wm_server_shutdown(void)
{
    assert(wm_server_context.initialized == 1);
    
    close(wm_server_context.server_socket);
    close(wm_server_context.input_socket);
    close(wm_server_context.socket_set);
    return 0;
}

int wm_server_main_loop(void)
{
    void*           argument_buffer;
    struct io_event events[32];
    int             i;
    
    argument_buffer = malloc(WM_MAX_MESSAGE_SIZE);
    if (!argument_buffer) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    while (wm_server_context.initialized) {
        int num_events = io_set_wait(wm_server_context.socket_set, &events[0], 32, 0);
        if (!num_events) {
            // why tho, timeout?
        }
        
        for (i = 0; i < num_events; i++) {
            if (events[i].iod == wm_server_context.server_socket) {
                handle_server_socket();
            }
            else if (events[i].iod == wm_server_context.input_socket) {
                handle_input_socket();
            }
            else {
                handle_client_event(events[i].iod, argument_buffer);
            }
        }
    }
    
    free(argument_buffer);
    return wm_server_shutdown();
}

int wm_server_register_protocol(wm_protocol_t* protocol)
{
    int i;
    
    for (i = 0; i < WM_MAX_PROTOCOLS; i++) {
        if (!wm_server_context.protocols[i]) {
            wm_server_context.protocols[i] = protocol;
            return 0;
        }
    }
    _set_errno(ENOSPC);
    return -1;
}

int wm_server_unregister_protocol(wm_protocol_t* protocol)
{
    int i;
    
    for (i = 0; i < WM_MAX_PROTOCOLS; i++) {
        if (wm_server_context.protocols[i] == protocol) {
            wm_server_context.protocols[i] = NULL;
            return 0;
        }
    }
    _set_errno(ENOENT);
    return -1;
}
