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
 * Wm Client Type Definitions & Structures
 * - This header describes the base client-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#include <assert.h>
#include <inet/socket.h>
#include <io.h>
#include "include/libwm_client.h"
#include "include/libwm_connection.h"
#include "include/libwm_crc.h"
#include "include/libwm_list.h"
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <threads.h>

typedef struct wm_client {
    uint32_t            client_id;
    enum wm_client_type type;
    int                 initialized;
    int                 socket;
    thrd_t              event_thread;
    struct wm_list      protocols;
} wm_client_t;

typedef void (*wm_invoke00_t)(void);
typedef void (*wm_invokeA0_t)(void*);

static int send_stream(wm_client_t* client, wm_message_t* message, 
    void* arguments, size_t argument_length)
{
    intmax_t bytes_written = send(client->socket, (const void*)message, 
        sizeof(wm_message_t), MSG_WAITALL);
    if (bytes_written == sizeof(wm_message_t) && message->length > sizeof(wm_message_t)) {
        bytes_written += send(client->socket, (const void*)arguments,
            argument_length, MSG_WAITALL);
    }
    return bytes_written != (sizeof(wm_message_t) + argument_length);
}

static int send_packet(wm_client_t* client, wm_message_t* message, 
    void* arguments, size_t argument_length)
{
    struct iovec  iov[2] = { 
        { .iov_base = message,   .iov_len = sizeof(wm_message_t) },
        { .iov_base = arguments, .iov_len = argument_length }
    };
    struct msghdr msg = {
        .msg_name       = NULL,
        .msg_namelen    = 0,
        .msg_iov        = &iov[0],
        .msg_iovlen     = 2,
        .msg_control    = NULL,
        .msg_controllen = 0,
        .msg_flags      = 0
    };
    intmax_t bytes_written = sendmsg(client->socket, &msg, MSG_WAITALL);
    return bytes_written != (sizeof(wm_message_t) + argument_length);
}

static int get_message_reply(wm_client_t* client, void* return_buffer, 
    size_t return_length)
{
    intmax_t bytes_read = recv(client->socket, return_buffer, 
        return_length, MSG_WAITALL);
    return bytes_read != return_length;
}

int wm_client_invoke_sync(wm_client_t* client, uint8_t protocol, uint8_t action, 
    void* arguments, size_t argument_length, void* return_buffer, 
    size_t return_length)
{
    int          status;
    wm_message_t message = { 
        .serial_no  = 0,
        .length     = (sizeof(wm_message_t) + argument_length),
        .ret_length = return_length,
        .crc        = 0,
        .protocol   = protocol,
        .action     = action
    };
    
    if (client->event_thread != UUID_INVALID) {
        return -1;
    }
    
    if (argument_length) {
        message.crc = crc16_generate((const unsigned char*)arguments, argument_length);
    }
    
    switch (client->type) {
        case wm_client_stream_based: {
            status = send_stream(client, &message, arguments, argument_length);
        } break;
        case wm_client_packet_based: {
            status = send_packet(client, &message, arguments, argument_length);
        } break;
    }
    
    if (!status && argument_length) {
        status = get_message_reply(client, return_buffer, return_length);
    }
    return status;
}

int wm_client_invoke_async(wm_client_t* client, uint8_t protocol, uint8_t action, 
    void* arguments, size_t argument_length, size_t return_length)
{
    int          status;
    wm_message_t message = { 
        .serial_no  = 0,
        .length     = (sizeof(wm_message_t) + argument_length),
        .ret_length = return_length,
        .crc        = 0,
        .protocol   = protocol,
        .action     = action
    };
    
    if (argument_length) {
        message.crc = crc16_generate((const unsigned char*)arguments, argument_length);
    }
    
    switch (client->type) {
        case wm_client_stream_based: {
            status = send_stream(client, &message, arguments, argument_length);
        } break;
        case wm_client_packet_based: {
            status = send_packet(client, &message, arguments, argument_length);
        } break;
    }
    return status;
}

static wm_protocol_function_t* get_protocol_action(wm_client_t* client, uint8_t protocol_id, uint8_t action_id)
{
    wm_protocol_t* protocol = (struct wm_protocol*)wm_list_lookup(&client->protocols, (int)(uint32_t)protocol_id);
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

static void invoke_action(wm_message_t* message, void* argument_buffer, wm_protocol_function_t* function)
{
    if (message->length > sizeof(wm_message_t)) {
        ((wm_invokeA0_t)function->address)(argument_buffer);
    }
    else {
        ((wm_invoke00_t)function->address)();
    }
}

static int wm_client_main_loop(void* context)
{
    wm_client_t* client = context;
    void*        argument_buffer;
    wm_message_t message;
    
    argument_buffer = malloc(WM_MAX_MESSAGE_SIZE);
    if (!argument_buffer) {
        client->event_thread = UUID_INVALID;
        _set_errno(ENOMEM);
        return -1;
    }
    
    while (1) {
        int status = -1;
        switch (client->type) {
            case wm_client_stream_based: {
                status = wm_connection_recv_stream(client->socket, &message, argument_buffer);
            } break;
            case wm_client_packet_based: {
                status = wm_connection_recv_packet(client->socket, &message, argument_buffer, NULL);
            } break;
        }
        
        if (!status) {
            wm_protocol_function_t* function = get_protocol_action(client, message.protocol, message.action);
            if (function) {
                invoke_action(&message, argument_buffer, function);
            }
        }
    }
    
    free(argument_buffer);
    return 0;
}

static int create_stream_socket(wm_client_configuration_t* config)
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

static int create_packet_socket(wm_client_configuration_t* config)
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

int wm_client_create(wm_client_configuration_t* config, wm_client_t** client_out)
{
    wm_client_t* client;
    
    client = (wm_client_t*)malloc(sizeof(wm_client_t));
    if (!client) {
        _set_errno(ENOMEM);
        return -1;
    }
    
    memset(client, 0, sizeof(wm_client_t));
    client->type = config->type;
    switch (config->type) {
        case wm_client_stream_based: {
            client->socket = create_stream_socket(config);
        } break;
        case wm_client_packet_based: {
            client->socket = create_packet_socket(config);
        } break;
    }
    
    if (client->socket == -1) {
        free(client);
        return -1;
    }
    
    if (config->async) {
        int status = thrd_create(&client->event_thread, wm_client_main_loop, client);
        if (status != thrd_success) {
            wm_client_shutdown(client);
            return -1;
        }
    }
    else {
        client->event_thread = UUID_INVALID;
    }
    
    *client_out = client;
    return 0;
}

int wm_client_register_protocol(wm_client_t* client, wm_protocol_t* protocol)
{
    if (!client || !protocol) {
        _set_errno(EINVAL);
        return -1;
    }
    
    wm_list_append(&client->protocols, &protocol->header);
    return 0;
}

int wm_client_unregister_protocol(wm_client_t* client, wm_protocol_t* protocol)
{
    if (!client || !protocol) {
        _set_errno(EINVAL);
        return -1;
    }
    
    wm_list_remove(&client->protocols, &protocol->header);
    return 0;
}

int wm_client_shutdown(wm_client_t* client)
{
    if (!client) {
        _set_errno(EINVAL);
        return -1;
    }
    
    if (client->event_thread != UUID_INVALID) {
        int exit_code;
        thrd_signal(client->event_thread, SIGKILL);
        thrd_join(client->event_thread, &exit_code);
    }
    
    close(client->socket);
    free(client);
    return 0;
}

